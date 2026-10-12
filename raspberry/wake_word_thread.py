#!/home/lang/car/venv/bin/python

import vosk
import json
import threading
import time
from queue import Empty
from pypinyin import lazy_pinyin
from difflib import SequenceMatcher


class WakeWordDetector(threading.Thread):
    def __init__(self, command_queue, model_path, keyword_path, audio_source,
                 shutdown_event, sensitivity=0.5):
        super().__init__()
        self.command_queue = command_queue
        self.model_path = model_path
        self.shutdown_event = shutdown_event
        self.keyword_path = keyword_path
        self.audio_source = audio_source
        self.sensitivity = sensitivity
        self.is_running = False
        self.is_detecting = False
        self.is_paused = False
        self.daemon = True
        self.model = None
        self.recognizer = None
        self.audio_queue = None
        self.keywords = []
        self.keyword_pinyins = []
        self.last_activity = time.time()
        # 【改进3】阈值从 0.6 提到 0.75，减少误唤醒
        self.similarity_threshold = 0.75
        # 【改进2】识别文本与唤醒词的字符数差允许范围
        self.length_tolerance = 1
        self.debug = True
        self.target_sr = 16000


    # ========== 【改进1】从 self.keywords 动态生成 KWS 词表 ==========
    def _build_kw_dict(self, base_score=0.5):
        return {kw: base_score for kw in self.keywords}

    # ========== 【改进2】匹配函数：长度过滤 + 拼音相似度 ==========
    def _match_keyword(self, text):
        """返回 (matched_keyword, score)，未匹配返回 (None, 0)"""
        if not text:
            return None, 0

        text_pinyin = ''.join(lazy_pinyin(text))
        best_kw = None
        best_score = 0.0

        for idx, kw in enumerate(self.keywords):
            # 长度过滤：汉字数差不超过 tolerance，挡掉“小”“小智”这种半截词
            if abs(len(text) - len(kw)) > self.length_tolerance:
                continue

            kw_pinyin = self.keyword_pinyins[idx]
            ratio = SequenceMatcher(None, text_pinyin, kw_pinyin).ratio()
            if ratio > best_score:
                best_score = ratio
                best_kw = kw

        if best_kw and best_score >= self.similarity_threshold:
            return best_kw, best_score
        return None, 0

    def run(self):
        print("🎤 [WakeWord] 初始化 Vosk 引擎...")
        print(f"    模型路径: {self.model_path}")
        print(f"    关键词文件: {self.keyword_path}")

        try:
            try:
                with open(self.keyword_path, 'r', encoding='utf-8') as f:
                    self.keywords = [line.strip() for line in f if line.strip()]
                print(f"    关键词: {self.keywords}")
            except Exception as e:
                print(f"❌ 读取关键词文件失败: {e}")
                return

            if not self.keywords:
                print("❌ 关键词文件为空，退出")
                return

            self.keyword_pinyins = [''.join(lazy_pinyin(kw)) for kw in self.keywords]
            print(f"    关键词拼音: {self.keyword_pinyins}")
            print(f"    相似度阈值: {self.similarity_threshold}")
            print(f"    长度容差: ±{self.length_tolerance} 字")

            try:
                vosk.SetLogLevel(0)
                self.model = vosk.Model(self.model_path)
                print("✅ Vosk 模型加载成功")
            except Exception as e:
                print(f"❌ Vosk 模型加载失败: {e}")
                return
            
            if self.shutdown_event.is_set():
                print("🛑 [WakeWord] 启动期间收到停止信号，直接退出")
                return
            self.is_running = True
            print("✅ [WakeWord] 引擎就绪，默认关闭唤醒检测（可通过命令 0x20 切换）")

            while self.is_running and not self.shutdown_event.is_set():
                self.last_activity = time.time()
                if not self.is_detecting or self.audio_queue is None:
                    if self.recognizer is not None:
                        self.recognizer = None
                    time.sleep(0.2)
                    continue

                if self.recognizer is None:
                    self.recognizer = vosk.KaldiRecognizer(self.model, self.target_sr)
                    # 【改进1】动态生成 KWS 词表
                    try:
                        kw_dict = self._build_kw_dict(base_score=0.5)
                        self.recognizer.SetKeywords(kw_dict)
                        if self.debug:
                            print(f"✅ KWS 模式已启用: {list(kw_dict.keys())}")
                    except Exception:
                        if self.debug:
                            print("⚠️ KWS 模式不可用，使用常规识别 + 拼音模糊匹配")

                try:
                    data = self.audio_queue.get(timeout=0.1)
                except Empty:
                    continue

                if self.is_paused:
                    continue

                try:
                    if self.recognizer.AcceptWaveform(data):
                        result = json.loads(self.recognizer.Result())
                        text = result.get('text', '')
                        if self.debug and text:
                            print(f"[Vosk 最终] {text}")

                        # 【改进2+3】长度过滤 + 提高阈值
                        matched, score = self._match_keyword(text)
                        if matched:
                            print(f"🔔 [WakeWord] 检测到唤醒词: {matched} "
                                f"(相似度: {score:.2f}, 识别文本: {text})")
                            self.command_queue.put({
                                "type": "internal_command",
                                "command": "wake_word_detected",
                                "keyword": matched
                            })
                    else:
                        partial = json.loads(self.recognizer.PartialResult())
                        partial_text = partial.get('partial', '')
                        if self.debug and partial_text:
                            print(f"[Vosk 部分] {partial_text}")
                        # 这里不做唤醒，PartialResult 只用于调试观察
                except Exception as e:
                    print(f"⚠️ [WakeWord] 处理异常: {e}")
        finally:
            self.cleanup()
            print("🛑 [WakeWord] 线程已退出")

    # ---------- 用户开关 ----------

    def toggle_detection(self):
        if self.is_detecting:
            self.stop_detection()
        else:
            self.start_detection()
        return self.is_detecting

    def start_detection(self):
        if not self.is_detecting:
            self.is_detecting = True
            self.is_paused = False
            self.recognizer = None
            if self.audio_source and self.audio_queue is None:
                self.audio_queue = self.audio_source.subscribe(maxsize=100)
            print("🔊 [WakeWord] 唤醒检测已开启")

    def stop_detection(self):
        if self.is_detecting:
            self.is_detecting = False
            self.is_paused = False
            if self.audio_source and self.audio_queue is not None:
                self.audio_source.unsubscribe(self.audio_queue)
                self.audio_queue = None
            print("🔇 [WakeWord] 唤醒检测已关闭")

    # ---------- 会话期间临时暂停 ----------

    def pause_detection(self):
        if self.is_paused:
            return
        self.is_paused = True
        print("⏸️ [WakeWord] 唤醒检测已暂停（订阅保留，音频源继续供流）")

    def resume_detection(self):
        if not self.is_paused:
            return
        self.is_paused = False

        # 丢弃暂停期间积压的旧音频，只处理恢复后的实时音频
        if self.audio_queue is not None:
            drained = 0
            while True:
                try:
                    self.audio_queue.get_nowait()
                    drained += 1
                except Empty:
                    break
            if drained and self.debug:
                print(f"🗑️ [WakeWord] 已丢弃暂停期间积压的 {drained} 包音频")

        if self.recognizer is not None:
            try:
                self.recognizer.Reset()
            except Exception:
                pass
        print("▶️ [WakeWord] 唤醒检测已恢复")

    def cleanup(self):
        if self.audio_source and self.audio_queue is not None:
            self.audio_source.unsubscribe(self.audio_queue)
            self.audio_queue = None