#!/home/lang/car/venv/bin/python

import threading
import time
from queue import Empty
import os
import numpy as np
from aip import AipSpeech


class BaiduVoiceDetector:          # ← 不再继承 Thread
    def __init__(self, command_queue, app_id, api_key, secret_key,
                 audio_source, shutdown_event, silence_timeout=0.8):
        # 删掉 super().__init__()
        # 删掉 self.is_running
        # 删掉 self.daemon
        self.command_queue = command_queue
        self.silence_timeout = silence_timeout
        self.shutdown_event = shutdown_event
        self.is_listening = False       # 保留：作为"是否已有识别任务在跑"的重入锁

        self.client = AipSpeech(app_id, api_key, secret_key)
        self.sample_rate = 16000
        self.channels = 1
        self.sample_width = 2
        self.packet_duration = 0.1
        self.audio_source = audio_source
        self.audio_queue = None
        self.debug_rms = False

        self.vad_energy_threshold = 500
        self.vad_min_speech = 0.3
        self.vad_max_record = 5.0

        self.last_activity = time.time()
        self._process_thread = None
    # ---------- 内部：安全提取识别文本 ----------
    def _extract_text(self, result):
        if not isinstance(result, dict):
            return None
        if result.get('err_no') != 0:
            return None
        res_list = result.get('result')
        if not isinstance(res_list, list) or len(res_list) == 0:
            return None
        text = res_list[0]
        if not isinstance(text, str):
            return None
        return text

    # ---------- 内部：识别一次 + 失败回退远场模型 ----------
    def _recognize(self, audio_data):
        try:
            result = self.client.asr(audio_data, 'pcm', self.sample_rate,
                                     {'dev_pid': 1537})
            text = self._extract_text(result)
            if text:
                print(f"✅ [BaiduVoice] 识别成功: '{text}'")
                return text
            if isinstance(result, dict):
                print(f"⚠️ [BaiduVoice] 1537 模型识别失败: "
                      f"err_no={result.get('err_no')}, "
                      f"err_msg={result.get('err_msg')}")
        except Exception as e:
            print(f"❌ [BaiduVoice] 1537 调用异常: {e}")

        try:
            result2 = self.client.asr(audio_data, 'pcm', self.sample_rate,
                                      {'dev_pid': 1536})
            text = self._extract_text(result2)
            if text:
                print(f"✅ [BaiduVoice] 第二次尝试成功: '{text}'")
                return text
            if isinstance(result2, dict):
                print(f"⚠️ [BaiduVoice] 1536 模型识别失败: "
                      f"err_no={result2.get('err_no')}, "
                      f"err_msg={result2.get('err_msg')}")
        except Exception as e:
            print(f"❌ [BaiduVoice] 1536 调用异常: {e}")

        return None

    # ---------- 内部：计算一包音频的 RMS 能量 ----------
    @staticmethod
    def _rms(data: bytes) -> float:
        if not data:
            return 0.0
        samples = np.frombuffer(data, dtype=np.int16)
        if samples.size == 0:
            return 0.0
        # 用 float32 避免 int16 平方溢出
        return float(np.sqrt(np.mean(samples.astype(np.float32) ** 2)))
    
    def stop(self):
        self.stop_listening()

    def start_listening(self, timeout=3.0, force_record=False):
        """
        timeout 参数现在只作为“最多录多久”的兜底，
        实际结束时间由 VAD 决定（连续静音超过 silence_timeout 就停）。
        """
        if self.is_listening:
            return
        if self.audio_source is None:
            print("❌ [BaiduVoice] 未配置音频源")
            self._notify_error("voice_no_mic")
            return

        print("🎤 [BaiduVoice] 准备接收语音指令...")
        self.is_listening = True
        self.audio_queue = self.audio_source.subscribe(maxsize=200)

        def process_thread():
            audio_data = b''
            try:
                if not force_record:
                    print("❌ [BaiduVoice] 非强制录音模式未实现")
                    self._notify_error("voice_not_understand")
                    return

                max_record = max(timeout, self.vad_max_record)
                print(f"🎤 [BaiduVoice] 开始录音，静音 {self.silence_timeout}s 自动结束，"
                      f"最长 {max_record}s")

                # 预热：丢前 2 包（约 0.2 秒），避免唤醒提示音残留
                for _ in range(2):
                    try:
                        self.audio_queue.get(timeout=0.1)
                    except Empty:
                        pass

                start_time = time.time()
                packet_count = 0
                speech_started = False      # 是否已经检测到说话
                silence_start = None        # 连续静音的起点

                while self.is_listening and not self.shutdown_event.is_set():
                    elapsed = time.time() - start_time
                    if elapsed >= max_record:
                        print(f"🎤 [BaiduVoice] 达到最长录音时长 {max_record}s，强制结束")
                        break

                    try:
                        raw_data = self.audio_queue.get(timeout=0.05)
                    except Empty:
                        continue

                    audio_data += raw_data
                    packet_count += 1

                    rms = self._rms(raw_data)
                    is_speech = rms >= self.vad_energy_threshold

                    if self.debug_rms:
                        print(f"    [VAD] rms={rms:7.1f} "
                              f"{'语音' if is_speech else '静音'}")

                    if is_speech:
                        speech_started = True
                        silence_start = None
                    else:
                        # 只有在“已经说过话”之后，静音才开始计时
                        if speech_started:
                            if silence_start is None:
                                silence_start = time.time()
                            elif (time.time() - silence_start) >= self.silence_timeout:
                                print(f"🎤 [BaiduVoice] 检测到连续静音 "
                                      f"{self.silence_timeout}s，提前结束录音")
                                break
                        else:
                            # 还没开口，允许最多等 vad_min_speech 秒
                            if elapsed >= self.vad_min_speech * 3:
                                print("🎤 [BaiduVoice] 一直没检测到说话，结束")
                                break

                elapsed = time.time() - start_time
                print(f"🎤 [BaiduVoice] 录音结束，耗时 {elapsed:.2f}s，"
                      f"共 {packet_count} 包（约 {len(audio_data)} 字节）")

                if not audio_data:
                    print("❌ [BaiduVoice] 未获取到任何音频数据")
                    self._notify_error("voice_not_understand")
                    return

                # 存 debug 音频
                try:
                    debug_dir = "/home/lang/car/debug"
                    os.makedirs(debug_dir, exist_ok=True)
                    with open(f"{debug_dir}/debug_audio.pcm", "wb") as f:
                        f.write(audio_data)
                    print(f"🎤 [调试] 音频已保存到 {debug_dir}/debug_audio.pcm")
                except Exception:
                    pass

                print("🎤 [BaiduVoice] 正在上传百度识别...")
                text = self._recognize(audio_data)
                if text:
                    self.command_queue.put({
                        "type": "internal_command",
                        "command": "voice_command",
                        "text": text
                    })
                else:
                    print("❌ [BaiduVoice] 两次尝试都未识别出内容")
                    self._notify_error("voice_not_understand")
            finally:
                if self.audio_queue is not None:
                    self.audio_source.unsubscribe(self.audio_queue)
                    self.audio_queue = None
                self.is_listening = False
                self.command_queue.put({
                    "type": "internal_command",
                    "command": "voice_listening_finished"
                })
                print("🎤 [BaiduVoice] 监听结束")

        thread = threading.Thread(target=process_thread)
        thread.daemon = True
        thread.start()
        self._process_thread = thread

    def _notify_error(self, tts_name):
        self.command_queue.put({
            "type": "internal_command",
            "command": "voice_error",
            "tts_name": tts_name
        })

    def stop_listening(self):
        self.is_listening = False