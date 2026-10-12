#!/home/lang/car/venv/bin/python

import threading
import time
import pyaudio
import numpy as np
from queue import Queue

try:
    import soxr
    HAS_SOXR = True
except ImportError:
    HAS_SOXR = False
    print("⚠️ [AudioSource] 未安装 soxr，如需重采样请执行: pip install soxr")


class AudioSource(threading.Thread):
    """
    独立麦克风采集线程：
      - 打开一次麦克风，按原生采样率读取
      - 用 soxr.ResampleStream 做真正的流式重采样到 16 kHz
      - 分发给所有订阅者（每个订阅者一个独立 Queue）
      - 无订阅者时自动释放麦克风，节省资源
    """

    TARGET_SR = 16000
    CHUNK_MS = 100        # 每次处理的音频时长（毫秒）

    def __init__(self, shutdown_event, debug=True):
        super().__init__()
        self.daemon = True
        self.shutdown_event = shutdown_event
        self.debug = debug
        self.target_sr = self.TARGET_SR
        self.last_activity = time.time()

        self.is_running = False
        self._subscribers = []                 # list[Queue]
        self._sub_lock = threading.Lock()

        self.pa = None
        self.stream = None
        self.resampler = None
        self.do_resample = False
        self.orig_sr = self.target_sr
        self.dev_idx = None
        self.orig_chunk_samples = self.target_sr * self.CHUNK_MS // 1000

    # ---------- 订阅接口 ----------

    def subscribe(self, maxsize=200):
        q = Queue(maxsize=maxsize)
        with self._sub_lock:
            self._subscribers.append(q)
        return q

    def unsubscribe(self, q):
        with self._sub_lock:
            if q in self._subscribers:
                self._subscribers.remove(q)

    def _has_subscribers(self):
        with self._sub_lock:
            return bool(self._subscribers)

    def _publish(self, data: bytes):
        with self._sub_lock:
            subs = list(self._subscribers)
        for q in subs:
            try:
                q.put_nowait(data)
            except Exception:
                pass

    # ---------- 设备选择 ----------

    def _select_device(self):
        temp_pa = pyaudio.PyAudio()
        try:
            input_devices = []
            for i in range(temp_pa.get_device_count()):
                dev = temp_pa.get_device_info_by_index(i)
                if dev['maxInputChannels'] > 0:
                    input_devices.append((i, dev['name'], dev['defaultSampleRate']))
                    if self.debug:
                        print(f"  设备 {i}: {dev['name']} "
                              f"(通道: {dev['maxInputChannels']}, "
                              f"SR: {dev['defaultSampleRate']})")
        finally:
            temp_pa.terminate()

        # 优先硬件设备
        priority_keywords = ['USB Composite Device', 'UACDemoV1.0']
        for kw in priority_keywords:
            for idx, name, sr in input_devices:
                if kw.lower() in name.lower() and 'hw:' in name:
                    return idx, name, sr

        # sysdefault > default
        for idx, name, sr in input_devices:
            if 'sysdefault' in name.lower():
                return idx, name, sr
        for idx, name, sr in input_devices:
            if 'default' in name.lower():
                return idx, name, sr
        return None

    # ---------- 主循环 ----------

    def run(self):
        self.is_running = True
        self.pa = pyaudio.PyAudio()

        try:
            dev = self._select_device()
            if dev is None:
                print("❌ [AudioSource] 未找到任何输入设备")
                return
            self.dev_idx, dev_name, dev_sr = dev
            self.orig_sr = int(dev_sr)
            self.orig_chunk_samples = int(self.orig_sr * self.CHUNK_MS / 1000)

            if 'hw:' in dev_name and self.orig_sr != self.target_sr:
                self.do_resample = True
                print(f"🎤 [AudioSource] 设备: [{self.dev_idx}] {dev_name} "
                    f"(原生 {self.orig_sr} Hz → 重采样 {self.target_sr} Hz)")
            else:
                self.do_resample = False
                self.orig_sr = self.target_sr
                self.orig_chunk_samples = self.target_sr * self.CHUNK_MS // 1000
                print(f"🎤 [AudioSource] 设备: [{self.dev_idx}] {dev_name} "
                    f"(原生 {self.orig_sr} Hz，无需重采样)")

            if self.do_resample and not HAS_SOXR:
                print("❌ [AudioSource] 需要重采样但未安装 soxr，采集线程退出")
                return

            print("✅ [AudioSource] 采集线程就绪")

            while not self.shutdown_event.is_set():
                # 无订阅者 → 关闭麦克风
                self.last_activity = time.time()
                if not self._has_subscribers():
                    if self.stream is not None:
                        self._close_stream()
                        print("🔇 [AudioSource] 无订阅者，麦克风已释放")
                    time.sleep(0.2)
                    continue

                # 有订阅者但流未打开 → 打开
                if self.stream is None:
                    if not self._open_stream():
                        time.sleep(0.5)
                        continue

                try:
                    data = self.stream.read(
                        self.orig_chunk_samples,
                        exception_on_overflow=False
                    )
                except OSError as e:
                    if "Input overflowed" in str(e):
                        continue
                    print(f"⚠️ [AudioSource] 读取异常: {e}")
                    self._close_stream()
                    time.sleep(0.2)
                    continue
                except Exception as e:
                    print(f"⚠️ [AudioSource] 读取异常: {e}")
                    self._close_stream()
                    time.sleep(0.2)
                    continue

                processed = self._process_chunk(data)
                if processed:
                    self._publish(processed)

        finally:
            self.cleanup()
            print("🛑 [AudioSource] 线程已退出")

    def _open_stream(self):
        try:
            self.stream = self.pa.open(
                rate=self.orig_sr,
                channels=1,
                format=pyaudio.paInt16,
                input=True,
                input_device_index=self.dev_idx,
                frames_per_buffer=self.orig_chunk_samples
            )
            if self.do_resample:
                # 真正的流式重采样器，块之间保持滤波器状态，无边界不连续
                self.resampler = soxr.ResampleStream(
                    self.orig_sr, self.target_sr, 1, dtype='int16'
                )
            print(f"✅ [AudioSource] 麦克风已打开 (设备 {self.dev_idx})")
            return True
        except Exception as e:
            print(f"❌ [AudioSource] 打开麦克风失败: {e}")
            self.stream = None
            return False

    def _close_stream(self):
        if self.stream is not None:
            try:
                self.stream.stop_stream()
                self.stream.close()
            except Exception:
                pass
            self.stream = None
        self.resampler = None

    def _process_chunk(self, data: bytes) -> bytes:
        if not self.do_resample:
            return data
        samples = np.frombuffer(data, dtype=np.int16)
        try:
            out = self.resampler.resample_chunk(samples)
        except Exception as e:
            print(f"⚠️ [AudioSource] 重采样异常: {e}")
            return b''
        return out.tobytes()

    def cleanup(self):
        self._close_stream()
        if self.pa:
            try:
                self.pa.terminate()
            except Exception:
                pass