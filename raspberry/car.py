#!/home/lang/car/venv/bin/python

import serial
import struct
import socket
import threading
import time
import os
import json
import pygame
import random
import re
import datetime
import tempfile
from pathlib import Path
from queue import Queue, Empty
from difflib import SequenceMatcher
from picamera2 import Picamera2
import cv2
from pypinyin import lazy_pinyin
from wake_word_thread import WakeWordDetector
from baidu_voice_thread import BaiduVoiceDetector
from tts_player import TTSPlayer
from aip import AipSpeech
from audio_source import AudioSource

# ------------------- 全局配置参数 -------------------
SERIAL_PORT = "/dev/ttyAMA10"
BAUDRATE = 115200
TCP_HOST = "0.0.0.0"
TCP_PORT = 8888
TCP_BUFFER_SIZE = 1024
MUSIC_DIR = "/home/lang/Music"
SUPPORT_FORMATS = (".mp3", ".wav", ".flac", ".m4a")
INITIAL_VOLUME = 100

# ------------------- 二进制指令定义 -------------------
CMD_SWITCH_PLAY_PAUSE = 0x07
CMD_PREV = 0x08
CMD_NEXT = 0x09
CMD_VOLUME = 0x0A
CMD_PLAY_SPECIFIC = 0x0B
CMD_SEEK = 0x0C
CMD_SWITCH_PLAY_MODE = 0x0D
CMD_PID_PARAM = 0x0E
CMD_LED_SWITCH = 0x06
CMD_TAKE_PHOTO = 0x0F
CMD_SWITCH_RECORD = 0x10
CMD_CAMERA_ON = 0x12
CMD_CAMERA_OFF = 0x13
CMD_TOGGLE_VOICE = 0x20
CMD_PWM_UPDATE = 0x19

PWM_PACKET_LEN = 9

# ------------------- 播放模式定义 -------------------
PLAY_MODE_LIST_LOOP = 0
PLAY_MODE_SINGLE_LOOP = 1
PLAY_MODE_RANDOM = 2

# ------------------- 全局变量 -------------------
# 注意：music_is_playing 已移入 VoiceSession
music_list = []
current_song_idx = 0
is_recording = False
volume = INITIAL_VOLUME / 100.0
pygame_init_ok = False
current_song_length = 0
current_song_position = 0

last_get_pos_time = 0
last_get_pos_value = 0
seek_offset = 0
music_loaded = False

play_mode = PLAY_MODE_LIST_LOOP
played_random_indices = []
played_random_indices_size = 10

# 看门狗配置：(线程对象, 超时秒数, 描述)
# 注意：线程对象在主函数里创建，所以这里先声明空列表，主函数里填充
watchdog_targets = []

# 看门狗线程的检查间隔
WATCHDOG_INTERVAL = 5.0
last_serial_activity = time.time()
last_command_activity = time.time()
last_tcp_activity = time.time()
last_music_activity = time.time()
last_camera_activity = time.time()
last_audio_activity = time.time()
last_wake_activity = time.time()

# --- 锁 ---
serial_write_lock = threading.Lock()
music_lock = threading.Lock()   # 保护音乐状态快照

command_queue = Queue()
connected_clients = []
clients_lock = threading.Lock()

ser = None

# 二进制帧
SENSOR_FRAME_HEADER = b'\xAA\x55'
SENSOR_CMD = 0x11
SENSOR_DATA_FORMAT = '<BffhhHHfB'
SENSOR_DATA_SIZE = struct.calcsize(SENSOR_DATA_FORMAT)
MIN_HEADER_LEN      = 4            # header(2) + len(2)
MAX_PAYLOAD_SIZE    = 256          # 合理上限，防垃圾数据

# 文本帧
TEXT_HEADER = b'$'
TEXT_TAIL = b'*\r\n'

latest_sensor = {"temp": 0, "humi": 0}

# 摄像头控制相关
camera_thread = None
camera_ready_event = threading.Event()     # 线程就绪
camera_enabled = threading.Event()         # 是否允许客户端连接/采集

# 全局退出事件：所有线程共享，主线程退出时 set()
shutdown_event = threading.Event()

command_map = {
    "切换灯光": ["切换灯光", "切换灯", "开关灯", "灯光"],
    "拍照": ["拍照", "照相", "拍张照"],
    "开始录像": ["开始录像", "录像开始", "录影"],
    "停止录像": ["停止录像", "录像停止", "关录像"],
    "时间": ["时间", "现在几点", "几点了"],
    "温度": ["温度", "多少度", "气温"],
    "湿度": ["湿度", "潮不潮", "干湿"],
    "停止音乐": ["停止播放", "别放了", "停止音乐", "暂停音乐"],
    "走Z字形": ["走一个Z字形", "走Z字", "Z字形", "走Z", "走之字形", "走一个Z"]
}

# 语音会话期间应被忽略的客户端音乐命令
_MUSIC_COMMANDS = {
    CMD_SWITCH_PLAY_PAUSE, CMD_PREV, CMD_NEXT,
    CMD_VOLUME, CMD_PLAY_SPECIFIC, CMD_SEEK, CMD_SWITCH_PLAY_MODE,
}

def _is_music_command(cmd_data):
    return len(cmd_data) >= 1 and cmd_data[0] in _MUSIC_COMMANDS

def make_frame(payload: bytes) -> bytes:
    """把 payload 包装成帧：AA 55 + LEN(4BE) + PAYLOAD + CRC8"""
    length_bytes = struct.pack('>I', len(payload))
    crc = crc8_update(0x00, length_bytes + payload)
    return b'\xAA\x55' + length_bytes + payload + bytes([crc])

def broadcast_json(msg_dict):
    """序列化成 JSON 并包帧广播"""
    payload = json.dumps(msg_dict).encode('utf-8')
    broadcast(make_frame(payload))

def send_json_to(conn, msg_dict):
    payload = json.dumps(msg_dict).encode('utf-8')
    return send_to(conn, make_frame(payload))

def make_frame_serial(payload: bytes) -> bytes:
    """串口帧：AA 55 + LEN(2LE) + PAYLOAD + CRC8"""
    length_bytes = struct.pack('<H', len(payload))
    crc = crc8_update(0x00, length_bytes + payload)
    return b'\xAA\x55' + length_bytes + payload + bytes([crc])

def send_to_stm32(payload: bytes):
    with serial_write_lock:
        if ser and ser.isOpen():
            ser.flushOutput()
            time.sleep(0.01)
            ser.write(make_frame_serial(payload))
        else:
            print("❌ 串口未打开")

def _end_voice_session():
    was_active = voice_session.active
    voice_session.exit()
    if was_active and wake_word_thread and wake_word_thread.is_detecting:
        wake_word_thread.resume_detection()
# ==================== 语音会话状态机 ====================
class VoiceSession:
    # 语音识别最长存活时间（秒），超时会将active恢复为False，表示不处于语音会话中
    SESSION_TIMEOUT = 30.0

    def __init__(self):
        self.active = False					#为True表示正处于语音会话状态，防止重复进入语音会话状态。并且在语音会话中，会忽略客户端的命令，不立刻执行（简化）不排队（延时用户体验不好）
        self.music_was_playing = False		#存储旧播放状态，和music_is_playing协作，开始、停止音乐
        self.music_is_playing = False 		#如果是True就要在唤醒时停止，语音识别结束后播放
        self.enter_time = 0.0				#记录进入语音识别的时间

    def enter(self):			#触发语音唤醒时调用，如果音乐正在播放，就暂停，在exit时再恢复
        if self.active:
            return
        self.active = True
        self.enter_time = time.time()
        if self.music_is_playing:
            pygame.mixer.music.pause()
            with music_lock:
                self.music_is_playing = False
            self.music_was_playing = True
    
    def exit(self):				#语音会话结束时调用，如果唤醒之前音乐正在播放，将恢复音乐播放
        if not self.active:
            return
        was = self.music_was_playing
        self.active = False
        self.music_was_playing = False
        self.enter_time = 0.0
        if was:
            try:
                pygame.mixer.music.unpause()
                with music_lock:
                    self.music_is_playing = True
            except Exception as e:
                print(f"❌ 恢复音乐失败: {e}")
    
    def discard(self):			#在语音为切歌或者暂停播放时调用，将music_was_playing设为False，这样exit时就不会恢复之前的音乐
        self.music_was_playing = False

    def check_timeout(self):	#命令处理线程中循环定期调用，语音会话超时会强制退出，返回 True 表示发生了超时
        if not self.active:
            return False
        if time.time() - self.enter_time > self.SESSION_TIMEOUT:
            print(f"⚠️ 语音会话超时（{self.SESSION_TIMEOUT}s），强制退出")
            self.exit()
            return True
        return False

voice_session = VoiceSession()

# ------------------- 初始化函数 -------------------
def init_serial():
    global ser
    try:
        ser = serial.Serial(
            port=SERIAL_PORT,
            baudrate=BAUDRATE,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            bytesize=serial.EIGHTBITS,
            timeout=0.1
        )
        if ser.isOpen():
            ser.reset_input_buffer()
            ser.flushInput()
            ser.flushOutput()
            print(f"✅ 串口初始化成功：{SERIAL_PORT}")
            return ser
    except Exception as e:
        print(f"❌ 串口初始化失败：{e}")
        return None


def init_pygame_safe():
    global pygame_init_ok
    try:
        if pygame.mixer.get_init():
            try:
                pygame.mixer.music.stop()
            except Exception:
                pass
            pygame.mixer.quit()
            time.sleep(0.1)
        pygame.mixer.init(frequency=44100, size=-16, channels=2, buffer=4096)
        pygame_init_ok = True
        print(f"✅ pygame音频引擎初始化成功，初始音量：{INITIAL_VOLUME}%")
        return True
    except Exception as e:
        pygame_init_ok = False
        print(f"❌ pygame初始化失败：{e}")
        return False


def get_song_length(song_path):
    try:
        sound = pygame.mixer.Sound(song_path)
        return int(sound.get_length() * 1000)
    except Exception as e:
        print(f"❌ 获取歌曲长度失败：{e}")
        return 0


def send_to(conn, data: bytes) -> bool:
    sock, addr, lock = conn
    with lock:
        try:
            sock.sendall(data)
            return True
        except OSError:
            return False

def broadcast(data: bytes):     #向所有客户端广播数据，失败的客户端会被移除
    with clients_lock:         
        snapshot = list(connected_clients)      #先做快照
    dead = [c for c in snapshot if not send_to(c, data)]
    if dead:
        with clients_lock:
            for c in dead:
                if c in connected_clients:
                    connected_clients.remove(c)
        for c in dead:
            try:
                c[0].close()
            except OSError:
                pass

# ------------------- 摄像头开启关闭函数 -------------------
def stop_camera():
    """关闭摄像头采集使能，线程和 Picamera2 保留"""
    camera_enabled.clear()
    print("📷 摄像头已关闭")


def start_camera():
    """开启摄像头采集使能，线程已在运行"""
    if camera_thread is None or not camera_thread.is_alive():
        print("❌ 摄像头线程未运行")
        return
    camera_enabled.set()
    print("📷 摄像头已开启")


# ------------------- PID 调试信息处理 -------------------
def handle_pid_debug_line(line):
    try:
        l_match = re.search(r'L:(-?\d+)', line)
        r_match = re.search(r'R:(-?\d+)', line)
        err_match = re.search(r'Err:(-?\d+\.?\d*)', line)
        out_match = re.search(r'Out:(-?\d+\.?\d*)', line)
        i_match = re.search(r'I:(-?\d+\.?\d*)', line)
        if l_match and r_match and err_match and out_match and i_match:
            print(f"[PID] L:{int(l_match.group(1)):5d} "
                  f"R:{int(r_match.group(1)):5d} "
                  f"Err:{float(err_match.group(1)):7.3f} "
                  f"Out:{float(out_match.group(1)):7.3f} "
                  f"I:{float(i_match.group(1)):7.3f}")
            return True
    except Exception as e:
        print(f"❌ 解析PID调试数据失败: {e}")
    return False


# ------------------- 校验和帧解析函数 -------------------
def crc8_update(crc, data):
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def parse_sensor_frame(frame):
    header   = frame[0:2]
    length   = struct.unpack('<H', frame[2:4])[0]
    payload  = frame[4:4 + length]
    checksum = frame[4 + length]

    if header != SENSOR_FRAME_HEADER:
        return False

    # CRC 覆盖 len + payload（长度随 len 变化）
    crc_calc = 0x00
    crc_calc = crc8_update(crc_calc, frame[2:4])
    crc_calc = crc8_update(crc_calc, payload)
    if crc_calc != checksum:
        print(f"❌ CRC校验失败: 收到{checksum:#x}, 计算{crc_calc:#x}")
        return False

    if length != SENSOR_DATA_SIZE:
        print(f"⚠️ 未知 payload 长度: {length}")
        return False

    cmd, humi, temp, enc_l, enc_r, pi_adc, motor_adc, yaw, mode = \
        struct.unpack(SENSOR_DATA_FORMAT, payload)

    if cmd != SENSOR_CMD:
        print(f"⚠️ 未知命令: {cmd:#x}")
        return False

    latest_sensor["temp"] = temp
    latest_sensor["humi"] = humi

    sensor_data = {
        "temp": temp, "humi": humi,
        "encoder_left_cnt": enc_l, "encoder_right_cnt": enc_r,
        "pi_bat_voltage": pi_adc, "motor_bat_voltage": motor_adc,
        "yaw": yaw, "mode": mode,
    }
    broadcast_json({"type": "sensor_data", "sensor": sensor_data})
    return True


def parse_text_frame(line_bytes):
    try:
        line_str = line_bytes.decode('utf-8')
    except UnicodeDecodeError:
        return
    if 'L:' in line_str and 'Err:' in line_str:
        handle_pid_debug_line(line_str)
    elif line_str.startswith('current='):
        print(f"[TURN] {line_str}")
    else:
        print(f"[STM32] {line_str}")


def music_status_thread(interval=0.2):
    """独立上报音乐状态，与传感器解耦。"""
    global last_music_activity
    try:
        while not shutdown_event.is_set():
            last_music_activity = time.time()
            try:
                with clients_lock:
                    has_client = bool(connected_clients)
                if has_client and pygame_init_ok:
                    with music_lock:
                        music_status = {
                            "position": current_song_position,
                            "length": current_song_length,
                            "percent": int((current_song_position / current_song_length * 100)
                                        if current_song_length > 0 else 0),
                            "current_index": current_song_idx,
                            "music_is_playing": voice_session.music_is_playing
                        }
                    status_msg = {
                        "type": "music_status",
                        "music": music_status
                    }
                    broadcast_json(status_msg)
            except Exception as e:
                print(f"❌ 音乐状态上报异常: {e}")

            time.sleep(interval)
    finally:
        print("🛑 [MusicStatus] 线程已退出")


# ------------------- 串口读取线程 -------------------
def serial_read_thread():
    global last_serial_activity
    buffer = b""
    try:
        while not shutdown_event.is_set():
            if not ser or not ser.isOpen():
                time.sleep(1)
                continue

            try:
                if ser.in_waiting > 0:
                    buffer += ser.read(ser.in_waiting)
                    last_serial_activity = time.time()
            except Exception as e:
                print(f"\n❌ 串口读取错误: {e}")
                time.sleep(0.1)
                continue

            while buffer:
                bin_idx = buffer.find(SENSOR_FRAME_HEADER)
                txt_idx = buffer.find(TEXT_HEADER)

                if bin_idx == -1 and txt_idx == -1:         #两个类型的帧头都没找到，说明是乱序数据，把除了最后一个字节的数据都丢掉
                    buffer = buffer[-1:]
                    break

                if bin_idx != -1 and (txt_idx == -1 or bin_idx < txt_idx):      #找到了二进制帧头，且比文本帧头更靠前，先处理二进制数据包

                    if bin_idx > 0:                         #丢弃帧头前的数据
                        buffer = buffer[bin_idx:]

                    if len(buffer) < MIN_HEADER_LEN:        #半包，先不处理，等待下一次循环再来处理
                        break
                    payload_len = struct.unpack('<H', buffer[2:4])[0]       #获取帧体长度
                    if payload_len == 0 or payload_len > MAX_PAYLOAD_SIZE:  #帧体长度异常，说明获取的帧头是错误的，丢弃一个字节，寻找下一个帧头
                        buffer = buffer[1:]                                 
                        continue
                    total_len = 4 + payload_len + 1                         #总长度 = 帧头(2) + 长度(2) + 帧体 + CRC(1)
                    if len(buffer) < total_len:                             #半包，先不处理，等待下一次循环再来处理
                        break
                    frame = buffer[:total_len]                              #取出完整数据包
                    if parse_sensor_frame(frame):
                        buffer = buffer[total_len:]
                    else:
                        buffer = buffer[1:]

                else:                                   # 处理文本数据包
                    buffer = buffer[txt_idx:]               #把帧头之前的数据从缓存区中去除
                    tail_idx = buffer.find(TEXT_TAIL, 1)
                    if tail_idx == -1:                          #没找到帧尾，说明是半包
                        break                                   #半包，就不处理，等待下一次循环再来处理
                    line = buffer[1:tail_idx]         #数据包完整，取帧内容（去掉帧头和帧尾）
                    buffer = buffer[tail_idx + len(TEXT_TAIL):] #把数据包从缓存区中去除
                    parse_text_frame(line)                      #解析文本数据包 

            time.sleep(0.05)

    finally:
        if ser and ser.isOpen():
            try:
                ser.close()
            except Exception:
                pass
        print("🛑 [Serial] 线程已退出")


# ------------------- 百度语音识别辅助函数 -------------------
def fuzzy_match(text, candidates, threshold=0.6):
    if not text:
        return None, 0
    text_pinyin = ''.join(lazy_pinyin(text))
    best_match = None
    best_score = 0
    for cand in candidates:
        cand_pinyin = ''.join(lazy_pinyin(cand))
        if cand_pinyin in text_pinyin:
            score = 1.0
        else:
            score = SequenceMatcher(None, text_pinyin, cand_pinyin).ratio()
        if score > best_score:
            best_score = score
            best_match = cand
    if best_score >= threshold:
        return best_match, best_score
    return None, 0


# ------------------- 音乐功能函数 -------------------
# 说明：
#   本模块所有【写操作】都只在 command_processor_thread 里执行（单写者）。
#   music_status_thread 是唯一的【跨线程读者】，通过 music_lock 拿快照。
#   music_is_playing 状态由 voice_session 持有，读写同样遵循单写者 + 快照读原则。

def load_music_list():
    global music_list
    try:
        music_dir = Path(MUSIC_DIR)
        if not music_dir.exists():
            print(f"❌ 音乐目录不存在：{MUSIC_DIR}")
            music_list = []
            return
        music_list = [f.name for f in music_dir.iterdir()
                      if f.suffix.lower() in SUPPORT_FORMATS]
        music_list.sort()
        print(f"✅ 加载音乐列表成功：共{len(music_list)}首")
    except Exception as e:
        print(f"❌ 加载音乐列表失败：{e}")
        music_list = []


def stop_music_safe():
    global music_loaded
    try:
        if pygame.mixer.music.get_busy():
            pygame.mixer.music.stop()
            time.sleep(0.05)
        music_loaded = False
        return True
    except Exception as e:
        print(f"❌ 停止音乐时出错：{e}")
        return False


def play_current_song_safe():
    """切歌/播放当前歌曲。只应在 command_processor_thread 中调用。"""
    global current_song_idx, current_song_length, current_song_position
    global last_get_pos_time, last_get_pos_value, seek_offset, music_loaded

    if not pygame_init_ok:
        if not init_pygame_safe():
            return {"status": "audio_engine_error", "current_song": ""}

    if not music_list:
        print("❌ 音乐列表为空，无法播放")
        return {"status": "empty", "current_song": ""}

    if current_song_idx < 0:
        current_song_idx = len(music_list) - 1
    elif current_song_idx >= len(music_list):
        current_song_idx = 0

    song_name = music_list[current_song_idx]
    song_path = os.path.join(MUSIC_DIR, song_name)

    stop_music_safe()
    length = get_song_length(song_path)

    try:
        pygame.mixer.music.load(song_path)
        pygame.mixer.music.set_volume(volume)
        pygame.mixer.music.play()
    except pygame.error as e:
        print(f"❌ 加载音乐文件失败：{e}")
        init_pygame_safe()
        return {"status": "load_error", "current_song": ""}

    with music_lock:
        current_song_length = length
        current_song_position = 0
        last_get_pos_time = 0
        last_get_pos_value = 0
        seek_offset = 0
        voice_session.music_is_playing = True
        music_loaded = True
        if play_mode == PLAY_MODE_RANDOM:
            add_played_random_index(current_song_idx)

    length_sec = length // 1000
    print(f"\n▶️  播放中：{song_name}（时长：{length_sec//60}:{length_sec%60:02d}，"
          f"音量：{volume*100:.0f}%，模式：{get_play_mode_name(play_mode)}）")
    return {"status": "playing", "current_song": song_name, "length": length}


def get_play_mode_name(mode):
    if mode == PLAY_MODE_LIST_LOOP:
        return "列表循环"
    elif mode == PLAY_MODE_SINGLE_LOOP:
        return "单曲循环"
    elif mode == PLAY_MODE_RANDOM:
        return "随机播放"
    return f"未知({mode})"


def add_played_random_index(index):
    global played_random_indices
    played_random_indices.append(index)
    if len(played_random_indices) > played_random_indices_size:
        played_random_indices.pop(0)


def get_next_song_index_by_mode():
    global current_song_idx, played_random_indices

    if not music_list:
        return 0

    if play_mode == PLAY_MODE_SINGLE_LOOP:
        return current_song_idx
    elif play_mode == PLAY_MODE_RANDOM:
        if len(music_list) <= 1:
            return 0
        all_indices = list(range(len(music_list)))
        unplayed_indices = [i for i in all_indices if i not in played_random_indices]
        if unplayed_indices:
            next_idx = random.choice(unplayed_indices)
            add_played_random_index(next_idx)
            return next_idx
        else:
            played_random_indices.clear()
            next_idx = random.choice(all_indices)
            add_played_random_index(next_idx)
            return next_idx
    else:
        return (current_song_idx + 1) % len(music_list)


def get_prev_song_index_by_mode():
    global current_song_idx

    if not music_list:
        return 0

    if play_mode == PLAY_MODE_SINGLE_LOOP:
        return current_song_idx
    elif play_mode == PLAY_MODE_RANDOM:
        return get_next_song_index_by_mode()
    else:
        return (current_song_idx - 1) % len(music_list)


def toggle_play_pause_safe():
    global music_loaded

    if not pygame_init_ok:
        return {"status": "audio_engine_error", "current_song": ""}

    if not music_list:
        return {"status": "empty", "current_song": ""}

    if not music_loaded:
        print("\n没有音乐加载，从头开始播放")
        return play_current_song_safe()

    try:
        if voice_session.music_is_playing:
            pygame.mixer.music.pause()
            with music_lock:
                voice_session.music_is_playing = False
            song_name = music_list[current_song_idx]
            print(f"\n⏸️  暂停播放：{song_name}（位置：{current_song_position/1000:.1f}秒，"
                  f"模式：{get_play_mode_name(play_mode)}）")
            return {"status": "paused", "current_song": song_name}
        else:
            pygame.mixer.music.unpause()
            with music_lock:
                voice_session.music_is_playing = True
            song_name = music_list[current_song_idx]
            print(f"\n▶️  继续播放：{song_name}（位置：{current_song_position/1000:.1f}秒，"
                  f"模式：{get_play_mode_name(play_mode)}）")
            return {"status": "playing", "current_song": song_name}
    except Exception as e:
        print(f"❌ 切换播放/暂停错误：{e}")
        return {"status": "toggle_error", "current_song": ""}


def prev_song_safe():
    global current_song_idx

    if not music_list:
        return {"status": "empty", "current_song": ""}

    current_song_idx = get_prev_song_index_by_mode()
    print(f"\n⏪  切换到上一首（{get_play_mode_name(play_mode)}）："
          f"[{current_song_idx}] {music_list[current_song_idx]}")
    return play_current_song_safe()


def next_song_safe():
    global current_song_idx

    if not music_list:
        return {"status": "empty", "current_song": ""}

    current_song_idx = get_next_song_index_by_mode()
    print(f"\n⏩  切换到下一首（{get_play_mode_name(play_mode)}）："
          f"[{current_song_idx}] {music_list[current_song_idx]}")
    return play_current_song_safe()


def play_specific_song_safe(song_idx):
    global current_song_idx

    if not music_list:
        return {"status": "empty", "current_song": "", "index": song_idx}

    if song_idx < 0 or song_idx >= len(music_list):
        print(f"❌ 无效的歌曲索引：{song_idx}（有效范围：0-{len(music_list)-1}）")
        return {"status": "invalid_index", "current_song": "", "index": song_idx}

    if play_mode == PLAY_MODE_RANDOM:
        add_played_random_index(song_idx)

    current_song_idx = song_idx
    print(f"\n🔄 切换到指定歌曲：[{current_song_idx}] {music_list[current_song_idx]}"
          f"（模式：{get_play_mode_name(play_mode)}）")
    return play_current_song_safe()


def set_volume_safe(new_volume):
    global volume

    if not pygame_init_ok:
        if not init_pygame_safe():
            return {"status": "audio_engine_error", "volume": 0}

    new_volume = max(0, min(100, new_volume))
    try:
        pygame.mixer.music.set_volume(new_volume / 100.0)
    except Exception as e:
        print(f"❌ 音量调节错误：{e}")
        return {"status": "volume_error", "volume": 0}
    volume = new_volume / 100.0
    print(f"\n🔊 音量调节：{new_volume}%")
    return {"status": "ok", "volume": new_volume}


def switch_play_mode_safe():
    global play_mode, played_random_indices

    if play_mode == PLAY_MODE_LIST_LOOP:
        play_mode = PLAY_MODE_SINGLE_LOOP
    elif play_mode == PLAY_MODE_SINGLE_LOOP:
        play_mode = PLAY_MODE_RANDOM
    else:
        play_mode = PLAY_MODE_LIST_LOOP

    if play_mode == PLAY_MODE_RANDOM:
        played_random_indices.clear()
        add_played_random_index(current_song_idx)

    mode_name = get_play_mode_name(play_mode)
    print(f"\n🔄 切换播放模式：{mode_name}")
    return {"status": "ok", "play_mode": play_mode, "mode_name": mode_name}


def seek_song_safe(percent):
    global current_song_position, last_get_pos_time, last_get_pos_value, seek_offset, music_loaded

    if not pygame_init_ok:
        return {"status": "audio_engine_error", "current_song": ""}

    if not music_list:
        return {"status": "empty", "current_song": ""}

    if current_song_length <= 0:
        return {"status": "length_error", "current_song": music_list[current_song_idx]}

    percent = max(0, min(100, percent))
    target_position = int(current_song_length * percent / 100)
    was_playing = voice_session.music_is_playing
    song_name = music_list[current_song_idx]
    song_path = os.path.join(MUSIC_DIR, song_name)

    try:
        pygame.mixer.music.load(song_path)
        pygame.mixer.music.set_volume(volume)
        pygame.mixer.music.play(start=target_position / 1000.0)
    except pygame.error as e:
        print(f"❌ 跳转失败：{e}")
        return {"status": "seek_error", "current_song": song_name}

    with music_lock:
        current_song_position = target_position
        seek_offset = target_position
        music_loaded = True
        voice_session.music_is_playing = was_playing

    if was_playing:
        print(f"\n⏱️  跳转到 {percent}% 位置（{target_position/1000:.1f}秒），继续播放")
        return {"status": "playing", "current_song": song_name,
                "position": target_position, "percent": percent}
    else:
        pygame.mixer.music.pause()
        print(f"\n⏱️  跳转到 {percent}% 位置（{target_position/1000:.1f}秒），已暂停")
        return {"status": "paused", "current_song": song_name,
                "position": target_position, "percent": percent}


def update_song_position():
    """由 command_processor_thread 定期调用（约 100ms 一次）。"""
    global current_song_position, last_get_pos_time, last_get_pos_value

    if not pygame_init_ok:
        return
    if not voice_session.music_is_playing or not music_loaded:
        return

    try:
        if not pygame.mixer.music.get_busy():
            return
        pos_ms = pygame.mixer.music.get_pos()
    except Exception as e:
        print(f"❌ 获取播放位置失败：{e}")
        return

    if pos_ms >= 0:
        with music_lock:
            current_song_position = seek_offset + pos_ms
            last_get_pos_value = pos_ms
            last_get_pos_time = time.time()
        if current_song_length > 0 and current_song_position >= (current_song_length - 100):
            with music_lock:
                current_song_position = current_song_length
            print(f"\n🎵 歌曲播放完毕，根据模式处理")
            handle_song_end()
    else:
        if current_song_length > 0 and current_song_position < (current_song_length - 100):
            with music_lock:
                current_song_position = current_song_length
            print(f"\n🎵 歌曲播放完毕（get_pos返回-1），根据模式处理")
            handle_song_end()


def handle_song_end():
    """在 command_processor_thread 内被 update_song_position 直接调用。"""
    try:
        if play_mode == PLAY_MODE_SINGLE_LOOP:
            print(f"\n🔂 单曲循环，重新播放：{music_list[current_song_idx]}")
            play_current_song_safe()
        else:
            next_song_safe()
    except Exception as e:
        print(f"❌ 处理歌曲结束失败：{e}")


def dynamic_speak(text):
    global tts_client
    temp_file = None
    try:
        result = tts_client.synthesis(text, 'zh', 1, {
            'spd': 5, 'pit': 5, 'vol': 9, 'per': 4
        }, timeout=5)                       # ← 网络超时
        if isinstance(result, dict):
            print(f"TTS合成失败: {result}")
            return
        with tempfile.NamedTemporaryFile(suffix='.mp3', delete=False) as f:
            f.write(result)
            temp_file = f.name
        sound = pygame.mixer.Sound(temp_file)
        channel = pygame.mixer.Channel(1)
        channel.play(sound)

        deadline = time.time() + 10.0       # ← 播放等待上限
        while channel and channel.get_busy():
            if shutdown_event.is_set() or time.time() > deadline:
                try:
                    channel.stop()
                except Exception:
                    pass
                break
            time.sleep(0.05)
    except Exception as e:
        print(f"动态TTS播放异常: {e}")
    finally:
        if temp_file and os.path.exists(temp_file):
            try:
                os.unlink(temp_file)
            except OSError:
                pass

# ==================== 内部命令分发 ====================
def _handle_internal_command(command):
    cmd = command.get("command", "")
    if cmd == "wake_word_detected":
        _handle_wake_word()
    elif cmd == "voice_listening_finished":         #语音识别结束后调用，负责退出语音会话状态
        _handle_voice_listening_finished()
    elif cmd == "voice_command":                    #成功识别到文本
        _handle_voice_command(command.get("text", ""))         
    elif cmd == "voice_error":                      #未识别到文本 
        _handle_voice_error(command.get("tts_name", "not_understand"))
    else:
        print(f"⚠️ 未知内部命令: {cmd}")

def _handle_voice_error(tts_name):
    """识别失败/无音频，播一句 TTS 让用户知道"""
    try:
        tts_player.play(tts_name)
    except Exception as e:
        print(f"❌ 语音错误提示播放失败: {e}")

def _handle_wake_word():
    print("🚗 小车被唤醒了！准备接收语音指令...")
    if voice_session.active:
        # 已在会话中，忽略重复唤醒，避免 TTS 重播和监听重启
        return
    voice_session.enter()
    wake_word_thread.pause_detection()
    try:
        tts_player.play_wait("wake_response")
        voice_detector.start_listening(timeout=3.5, force_record=True)
    except Exception as e:
        print(f"❌ 唤醒处理异常: {e}")
        voice_session.exit()   # 异常时立刻退出会话
        wake_word_thread.resume_detection()
        raise


def _handle_voice_listening_finished():
    """监听结束（超时或已处理完一条语音命令）→ 恢复音乐。"""
    _end_voice_session()


def _handle_voice_command(text):
    print("\n" + "=" * 50)
    print(f"🎯 [语音识别结果] 你说的是：\"{text}\"")
    print("=" * 50 + "\n")

    result_msg = {
        "type": "voice_result",
        "data": {"text": text, "timestamp": time.time()}
    }
    broadcast_json(result_msg)

    _dispatch_voice_text(text)



def _dispatch_voice_text(text):
    """根据识别到的文本执行命令。可以在任意分支 return。
    接管音乐控制的语音命令（停止/切歌）需调用 voice_session.discard()。"""
    global is_recording

    if not text:
        tts_player.play("not_understand")
        return

    matched_cmd = None
    for cmd, aliases in command_map.items():
        match, score = fuzzy_match(text, aliases, threshold=0.7)
        if match:
            matched_cmd = cmd
            break

    if matched_cmd == "切换灯光":
        tts_player.play("switch_light")
        with serial_write_lock:
            if ser and ser.isOpen():
                ser.flushOutput()
                time.sleep(0.01)
                send_to_stm32(bytes([CMD_LED_SWITCH]))
        return

    if matched_cmd == "拍照":
        tts_player.play("take_photo")
        broadcast(make_frame(bytes([CMD_TAKE_PHOTO])))
        return

    if matched_cmd == "开始录像":
        if is_recording:
            tts_player.play("already_recording")
        else:
            tts_player.play("start_recording")
            is_recording = True
            broadcast(make_frame(bytes([CMD_SWITCH_RECORD])))
        return

    if matched_cmd == "停止录像":
        if is_recording:
            tts_player.play("stop_recording")
            is_recording = False
            broadcast(make_frame(bytes([CMD_SWITCH_RECORD])))
        else:
            tts_player.play("not_recording")
        return

    if matched_cmd == "时间":
        now = datetime.datetime.now()
        hour = now.hour
        minute = now.minute
        period = "上午" if hour < 12 else "下午"
        if hour > 12:
            hour -= 12
        time_str = f"{period}{hour}点"
        if minute != 0:
            time_str += f"{minute}分"
        dynamic_speak(f"当前时间{time_str}")
        return

    if matched_cmd == "温度":
        temp = latest_sensor.get("temp", 0.0)
        dynamic_speak(f"当前温度是{temp:.1f}度")
        return

    if matched_cmd == "湿度":
        humi = latest_sensor.get("humi", 0.0)
        dynamic_speak(f"当前湿度是百分之{humi:.1f}")
        return

    if matched_cmd == "停止音乐":
        if voice_session.music_is_playing or pygame.mixer.music.get_busy():
            stop_music_safe()
            with music_lock:
                voice_session.music_is_playing = False
            voice_session.discard()
            tts_player.play("music_stopped")
        else:
            tts_player.play("not_playing")
        return

    if "播放" in text:
        parts = text.split("播放")
        if len(parts) > 1:
            song_query = parts[-1].strip()
            if song_query:
                best_song = None
                best_score = 0
                text_pinyin = ''.join(lazy_pinyin(song_query))
                for song in music_list:
                    song_name = os.path.splitext(song)[0]
                    song_pinyin = ''.join(lazy_pinyin(song_name))
                    score = SequenceMatcher(None, text_pinyin, song_pinyin).ratio()
                    if score > best_score:
                        best_score = score
                        best_song = song
                if best_song and best_score >= 0.6:
                    idx = music_list.index(best_song)
                    voice_session.discard()
                    play_specific_song_safe(idx)
                    return

    tts_player.play("not_understand")


# ==================== 命令处理线程 ====================
def command_processor_thread():
    """所有音乐操作（包括位置更新、歌曲结束、延迟任务）都在这个线程内串行执行，
    因此音乐状态不需要跨线程写锁保护（只对快照读加锁）。"""
    global last_command_activity
    last_tick = time.time()

    try:
        while not shutdown_event.is_set():
            try:
                command = command_queue.get(timeout=0.05)
                if command is not None:                     #如果有命令，就更新看门狗最后活动时间
                    last_command_activity = time.time()     
            except Empty:
                command = None

            if command:
                try:
                    if isinstance(command, dict) and command.get("type") == "internal_command":     #dict是字典（JSON对象）
                        _handle_internal_command(command)           #处理内部命令（语音、唤醒词命令）
                    else:
                        conn, cmd_data = command
                        # 语音会话中忽略客户端的音乐命令，避免与语音会话抢占/恢复逻辑冲突
                        if voice_session.active and _is_music_command(cmd_data):
                            print(f"⚠️ 语音会话中忽略客户端音乐命令: {cmd_data[0]:#x}")
                        else:
                            process_client_command(conn, cmd_data)
                except Exception as e:
                    print(f"❌ 命令处理异常: {e}")

            now = time.time()
            if now - last_tick >= 0.1:
                try:
                    if voice_session.check_timeout():
                        if wake_word_thread and wake_word_thread.is_detecting:
                            wake_word_thread.resume_detection()
                except Exception as e:
                    print(f"❌ 会话超时检查异常: {e}")
                try:
                    update_song_position()
                except Exception as e:
                    print(f"❌ 位置更新异常: {e}")
                last_tick = now
                last_command_activity = time.time()                 #更新看门狗最后活动时间
    finally:
        print("🛑 [Command] 线程已退出")

def process_client_command(conn, cmd_data):
    """处理客户端命令"""
    try:
        if len(cmd_data) == PWM_PACKET_LEN and cmd_data[0] == CMD_PWM_UPDATE:
            try:
                right_pwm = struct.unpack('<I', cmd_data[1:5])[0]
                left_pwm = struct.unpack('<I', cmd_data[5:9])[0]
                print(f"📤 转发0x19命令: rightPWM={right_pwm}, leftPWM={left_pwm}")
            except Exception:
                pass
            send_to_stm32(cmd_data)


        elif len(cmd_data) == 13 and cmd_data[0] == CMD_PID_PARAM:
                try:
                    Kp      = struct.unpack('<f', cmd_data[1:5])[0]
                    Yaw_Kp  = struct.unpack('<f', cmd_data[5:9])[0]
                    PWM_Lim = struct.unpack('<i', cmd_data[9:13])[0]
                    print(f"📥 收到PID参数: Kp={Kp}, Yaw_Kp={Yaw_Kp}, PWM_Lim={PWM_Lim}")
                except Exception as e:
                    print(f"❌ 解析PID参数失败: {e}")
                send_to_stm32(cmd_data)          # ← 直接调用

        elif len(cmd_data) == 1:
            cmd = cmd_data[0]

            if cmd == CMD_SWITCH_PLAY_PAUSE:
                toggle_play_pause_safe()
            elif cmd == CMD_PREV:
                prev_song_safe()
            elif cmd == CMD_NEXT:
                next_song_safe()
            elif cmd == CMD_SWITCH_PLAY_MODE:
                switch_play_mode_safe()
            elif cmd == CMD_CAMERA_ON:
                start_camera()
            elif cmd == CMD_CAMERA_OFF:
                stop_camera()
            elif cmd == CMD_TOGGLE_VOICE:
                if wake_word_thread:
                    new_state = wake_word_thread.toggle_detection()
            else:
                send_to_stm32(cmd_data)
                return

        elif len(cmd_data) == 2:
            cmd = cmd_data[0]

            if cmd == CMD_VOLUME:
                set_volume_safe(cmd_data[1])
            elif cmd == CMD_PLAY_SPECIFIC:
                play_specific_song_safe(cmd_data[1])
            elif cmd == CMD_SEEK:
                seek_song_safe(cmd_data[1])

    except Exception as e:
        print(f"❌ 指令处理错误：{e}")
        try:
            send_json_to(conn, {"type": "error", "data": "指令处理失败"})
        except Exception as e2:
            print(f"❌ 发送错误反馈失败：{e2}")


# ------------------- TCP客户端处理线程 -------------------
def handle_client(client_socket, client_addr):
    global is_recording, wake_word_thread

    print(f"\n🔌 Qt客户端已连接：{client_addr}")
    conn = (client_socket, client_addr, threading.Lock())
    with clients_lock:
        connected_clients.append(conn)

    if not pygame_init_ok:
        init_pygame_safe()

    if not send_json_to(conn, {"type": "music_list", "data": music_list}):
        print(f"❌ 发送初始数据失败，关闭连接 {client_addr}")
        with clients_lock:
            if conn in connected_clients:
                connected_clients.remove(conn)
        try:
            client_socket.close()
        except OSError:
            pass
        return
    print(f"📤 发送音乐列表: {music_list}")

    cmd_buffer = b""

    try:
        client_socket.settimeout(1.0)
        while not shutdown_event.is_set():
            try:
                data = client_socket.recv(TCP_BUFFER_SIZE)
                if not data:
                    print(f"\n🔌 客户端{client_addr}断开连接")
                    break

                cmd_buffer += data

                # ---------- 帧解析：AA 55 + LEN(4BE) + PAYLOAD + CRC8 ----------
                while True:
                    # 1. 找帧头
                    idx = cmd_buffer.find(b'\xAA\x55')
                    if idx == -1:
                        cmd_buffer = cmd_buffer[-1:] if len(cmd_buffer) > 1 else cmd_buffer
                        break
                    if idx > 0:
                        cmd_buffer = cmd_buffer[idx:]

                    # 2. 长度字段到齐？
                    if len(cmd_buffer) < 6:
                        break

                    payload_len = struct.unpack('>I', cmd_buffer[2:6])[0]
                    if payload_len == 0 or payload_len > 65536:
                        cmd_buffer = cmd_buffer[1:]
                        continue

                    total_len = 2 + 4 + payload_len + 1

                    # 3. 整帧到齐？
                    if len(cmd_buffer) < total_len:
                        break

                    # 4. CRC 校验
                    recv_crc = cmd_buffer[total_len - 1]
                    calc_crc = crc8_update(0x00, cmd_buffer[2:6 + payload_len])
                    if recv_crc != calc_crc:
                        print(f"❌ 客户端命令CRC失败")
                        cmd_buffer = cmd_buffer[1:]
                        continue

                    # 5. 提取 payload 入队
                    payload = cmd_buffer[6:6 + payload_len]
                    cmd_buffer = cmd_buffer[total_len:]
                    command_queue.put((conn, bytes(payload)))

            except socket.timeout:
                continue
            except Exception as e:
                print(f"❌ 客户端连接错误：{e}")
                break
    finally:
        with clients_lock:
            if conn in connected_clients:
                connected_clients.remove(conn)
        try:
            client_socket.close()
        except OSError:
            pass
        print(f"🛑 [Client {client_addr}] 线程已退出")

    with clients_lock:
        is_last_client = not connected_clients

    if is_last_client:
        print("📱 所有客户端已断开，正在关闭高功耗功能...")
        if is_recording:
            is_recording = False
            print("🛑 已停止录像")
        stop_camera()          # 只是 camera_enabled.clear()
        if wake_word_thread and wake_word_thread.is_detecting:
            wake_word_thread.stop_detection()
            print("🔇 已关闭语音识别")


# ------------------- TCP服务器线程 -------------------
def tcp_server_thread():
    global last_tcp_activity
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.settimeout(0.5)

    try:
        server_socket.bind((TCP_HOST, TCP_PORT))
        server_socket.listen(5)
        print(f"🌐 TCP服务器已启动，监听端口：{TCP_PORT}")

        while not shutdown_event.is_set():
            try:
                client_socket, client_addr = server_socket.accept()
                last_tcp_activity = time.time()
                print(f"\n🔗 接受客户端连接：{client_addr}")
                threading.Thread(target=handle_client,
                                 args=(client_socket, client_addr),
                                 daemon=True).start()
            except socket.timeout:
                last_tcp_activity = time.time()
                continue
            except Exception as e:
                print(f"❌ 接受连接错误：{e}")
                continue    

    except Exception as e:
        print(f"❌ TCP服务器错误：{e}")
    finally:
        server_socket.close()


# ------------------- 摄像头线程 -------------------
def camera_server_thread():
    global last_camera_activity
    picam2 = None
    server_socket = None
    try:
        picam2 = Picamera2()
        config = picam2.create_preview_configuration(
            main={"size": (640, 480), "format": "RGB888"})
        picam2.configure(config)
        picam2.start()
        camera_ready_event.set()
        print("✅ 摄像头初始化成功")

        server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server_socket.bind(("0.0.0.0", 8889))
        server_socket.listen(1)
        server_socket.settimeout(0.5)
        print("🌐 摄像头服务器启动，端口：8889")

        time.sleep(0.5)

        while not shutdown_event.is_set():
            # 摄像头未开启时，不接受客户端，也不采集
            last_camera_activity = time.time()
            if not camera_enabled.is_set():
                time.sleep(0.2)
                continue

            try:
                server_socket.settimeout(0.5)
                client_socket, client_addr = server_socket.accept()
                if not camera_enabled.is_set():
                    print("⚠️ 摄像头未开启，拒绝连接")
                    client_socket.close()
                    continue
                print(f"🔗 摄像头客户端连接：{client_addr}")
                client_socket.settimeout(0.5)
                with client_socket:
                    TARGET_FPS = 30
                    FRAME_INTERVAL = 1.0 / TARGET_FPS
                    next_frame_time = time.monotonic()

                    while not shutdown_event.is_set() and camera_enabled.is_set():
                        try:
                            now = time.monotonic()
                            if now < next_frame_time:
                                time.sleep(next_frame_time - now)
                                continue

                            frame = picam2.capture_array()
                            ret, jpeg = cv2.imencode(
                                '.jpg', frame, [cv2.IMWRITE_JPEG_QUALITY, 70])
                            if not ret:
                                next_frame_time += FRAME_INTERVAL
                                continue

                            ts_us = int(time.monotonic() * 1_000_000)
                            body = struct.pack('>Q', ts_us) + jpeg.tobytes()
                            client_socket.sendall(make_frame(body))

                            next_frame_time += FRAME_INTERVAL
                            now = time.monotonic()
                            if next_frame_time < now - FRAME_INTERVAL:
                                next_frame_time = now + FRAME_INTERVAL

                        except socket.timeout:
                            continue
                        except (BrokenPipeError, ConnectionResetError):
                            print("🔌 摄像头客户端断开连接")
                            break
                        except Exception as e:
                            print(f"❌ 视频流发送异常: {e}")
                            break
            except socket.timeout:
                continue
            except Exception as e:
                if not shutdown_event.is_set():
                    print(f"❌ 摄像头服务器异常: {e}")
                break
    except Exception as e:
        print(f"❌ 摄像头线程初始化失败: {e}")
        import traceback
        traceback.print_exc()
    finally:
        if server_socket:
            server_socket.close()
        if picam2:
            picam2.stop()
            picam2.close()
        print("🛑 [Camera] 线程已退出")

#看门狗线程
def watchdog_thread():
    global ser, last_serial_activity, last_command_activity
    global last_tcp_activity, last_music_activity, last_camera_activity
    while not shutdown_event.is_set():
        now = time.time()

        # --- 串口：自动重开 ---
        if now - last_serial_activity > 5.0:
            print(f"⚠️ [Watchdog] 串口超时，尝试重开")
            try:
                if ser and ser.isOpen():
                    ser.close()
                time.sleep(0.5)
                ser = init_serial()
            except Exception as e:
                print(f"❌ [Watchdog] 串口重开失败: {e}")
            last_serial_activity = now

        # --- 命令处理：只报警 ---
        if now - last_command_activity > 30.0:
            print(f"⚠️ [Watchdog] 命令处理超时，可能卡在长命令")
            last_command_activity = now

        # --- 主服务器：只报警 ---
        if now - last_tcp_activity > 5.0:
            print(f"⚠️ [Watchdog] TCP 服务器超时")
            last_tcp_activity = now

        # --- 音乐状态：只报警 ---
        if now - last_music_activity > 5.0:
            print(f"⚠️ [Watchdog] 音乐状态超时")
            last_music_activity = now

        # --- 摄像头：只报警 ---
        if now - last_camera_activity > 5.0:
            print(f"⚠️ [Watchdog] 摄像头超时")
            last_camera_activity = now

        # --- 音频：只报警 ---
        if now - audio_source_t.last_activity > 5.0:
            print(f"⚠️ [Watchdog] 音频采集超时")
            audio_source_t.last_activity = now

        # --- 唤醒词：重置 is_detecting ---
        if now - wake_word_thread.last_activity > 10.0:
            print(f"⚠️ [Watchdog] 唤醒词超时，重置检测")
            if wake_word_thread.is_detecting:
                wake_word_thread.stop_detection()
            wake_word_thread.last_activity = now

        # --- 百度语音：重置 is_listening ---
        if voice_detector.is_listening:
            if now - voice_detector.last_activity > 30.0:
                print(f"⚠️ [Watchdog] 语音识别超时，强制重置")
                voice_detector.is_listening = False
                if voice_detector.audio_queue is not None:
                    try:
                        voice_detector.audio_source.unsubscribe(voice_detector.audio_queue)
                    except Exception:
                        pass
                    voice_detector.audio_queue = None

        shutdown_event.wait(WATCHDOG_INTERVAL)
# ------------------- 主函数 -------------------
if __name__ == "__main__":
    ser = init_serial()
    if not ser:
        print("❌ 串口初始化失败，程序退出")
        exit(1)

    init_pygame_safe()
    load_music_list()
    tts_player = TTSPlayer(audio_dir="/home/lang/car/tts", shutdown_event=shutdown_event)

    BAIDU_APP_ID = "121439094"
    BAIDU_API_KEY = "1IKtkqkG5n2POLNsGp0l83uM"
    BAIDU_SECRET_KEY = "u86diCU47JyaEsYqMrMvGEKFsPlVDpMm"

    BAIDU_TTS_APP_ID = "122306686"
    BAIDU_TTS_API_KEY = "MilNY56k0r5wJZgOcMw73jws"
    BAIDU_TTS_SECRET_KEY = "8GG0V8MonYun0QOyQ5mdP67bdniSwzJV"

    tts_client = AipSpeech(BAIDU_TTS_APP_ID, BAIDU_TTS_API_KEY, BAIDU_TTS_SECRET_KEY)

    # 启动所有线程
    serial_t = threading.Thread(target=serial_read_thread, daemon=True)
    serial_t.start()
    command_t = threading.Thread(target=command_processor_thread, daemon=True)
    command_t.start()
    tcp_t = threading.Thread(target=tcp_server_thread, daemon=True)
    tcp_t.start()
    music_t = threading.Thread(target=music_status_thread, daemon=True)
    music_t.start()
    camera_thread = threading.Thread(target=camera_server_thread, daemon=True)
    camera_thread.start()
    camera_ready_event.wait(timeout=5.0)        #等待摄像头线程初始化完成
    audio_source_t = AudioSource(shutdown_event=shutdown_event, debug=True)
    audio_source_t.start()

    voice_detector = BaiduVoiceDetector(
        command_queue=command_queue,
        app_id=BAIDU_APP_ID,
        api_key=BAIDU_API_KEY,
        secret_key=BAIDU_SECRET_KEY,
        audio_source=audio_source_t,
        shutdown_event=shutdown_event,
        silence_timeout=1.0
    )

    wake_word_thread = WakeWordDetector(
        command_queue=command_queue,
        model_path="/home/lang/car/wake-model/vosk-model-small-cn-0.22",
        keyword_path="/home/lang/car/wake-model/keyword.txt",
        audio_source=audio_source_t,
        sensitivity=0.5,
        shutdown_event=shutdown_event
    )
    wake_word_thread.start()

    watchdog_t = threading.Thread(target=watchdog_thread, daemon=True)
    watchdog_t.start()
    
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        shutdown_event.set()

        if pygame_init_ok:
            try:
                pygame.mixer.music.stop()
                pygame.mixer.quit()
            except Exception:
                pass

        audio_source_t.join(timeout=2.0)
        wake_word_thread.join(timeout=2.0)
        serial_t.join(timeout=2.0)
        command_t.join(timeout=2.0)
        tcp_t.join(timeout=2.0)
        music_t.join(timeout=2.0)
        camera_thread.join(timeout=3.0)
        watchdog_t.join(timeout=2.0)
        print("✅ 程序已退出")