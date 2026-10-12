#!/home/lang/car/venv/bin/python

import pygame
import os
import threading
import time


class TTSPlayer:
    # 预合成提示音专用通道号，避免和 dynamic_speak 抢通道
    TTS_CHANNEL_ID = 0

    def __init__(self, shutdown_event=None, audio_dir="/home/lang/car/tts"):
        self.audio_dir = audio_dir
        self.is_playing = False
        self._lock = threading.Lock()
        self.shutdown_event = shutdown_event

        # name -> Sound 对象，预加载到内存
        self.sound_map = {}
        self._scan_audio_files()

        print(f"🔊 [TTSPlayer] 初始化完成，找到 {len(self.sound_map)} 个音频文件")

    def _scan_audio_files(self):
        if not os.path.exists(self.audio_dir):
            os.makedirs(self.audio_dir)
            return

        for filename in os.listdir(self.audio_dir):
            if filename.endswith(('.mp3', '.wav')):
                name = os.path.splitext(filename)[0]
                path = os.path.join(self.audio_dir, filename)
                try:
                    self.sound_map[name] = pygame.mixer.Sound(path)
                except Exception as e:
                    print(f"⚠️ [TTSPlayer] 预加载失败 {filename}: {e}")

    def play(self, name, wait=False):
        if name not in self.sound_map:
            print(f"❌ [TTSPlayer] 未找到音频: {name}")
            return False

        # 防止 TTS 自我重叠
        with self._lock:
            if self.is_playing:
                print("⚠️ [TTSPlayer] 正在播放中，忽略本次请求")
                return False
            self.is_playing = True

        sound = self.sound_map[name]

        def play_thread():
            try:
                channel = pygame.mixer.Channel(self.TTS_CHANNEL_ID)
                channel.play(sound)
                if wait:
                    while channel.get_busy():
                        if self.shutdown_event and self.shutdown_event.is_set():
                            channel.stop()
                            break
                        time.sleep(0.05)
            except Exception as e:
                print(f"❌ [TTSPlayer] 播放失败: {e}")
            finally:
                with self._lock:
                    self.is_playing = False

        thread = threading.Thread(target=play_thread)
        thread.daemon = True
        thread.start()

        if wait:
            thread.join()

        return True

    def play_wait(self, name):
        return self.play(name, wait=True)

    def add_audio(self, name, audio_path):
        if os.path.exists(audio_path):
            try:
                self.sound_map[name] = pygame.mixer.Sound(audio_path)
                return True
            except Exception as e:
                print(f"❌ [TTSPlayer] 加载失败: {e}")
        return False