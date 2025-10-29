import socket
import struct
import json
import time
import threading
import base64

import cv2
import numpy as np
import torch
from ultralytics import YOLO

# =========================
# YOLO 설정
# =========================
DEVICE = 0 if torch.cuda.is_available() else "cpu"
YOLO_WEIGHTS = "yolov8n.pt"     # 학습 후 모델 파일로 교체하세요

_yolo_model = None
def get_yolo():
    global _yolo_model
    if _yolo_model is None:
        print(f"[YOLO] Loading {YOLO_WEIGHTS} on {DEVICE} ...")
        _yolo_model = YOLO(YOLO_WEIGHTS)
    return _yolo_model

def infer_image(img_bgr, camera_type=None):
    """이미지 1장 추론 (결과는 내부 통일 포맷으로 반환)"""
    t0 = time.time()

    # 방어 코드
    if img_bgr is None or img_bgr.size == 0:
        return {
            "ok": False,
            "camera_type": camera_type,
            "result": "defective",
            "defect_score": 0.0,
            "classes": [],
            "bboxes": [],
            "inference_ms": 0.0,
            "model_version": f"yolo8-{YOLO_WEIGHTS}",
            "error": "invalid image"
        }

    img_rgb = cv2.cvtColor(img_bgr, cv2.COLOR_BGR2RGB)
    model = get_yolo()
    results = model.predict(
        source=img_rgb,
        imgsz=640,
        conf=0.25,
        iou=0.45,
        device=DEVICE,
        verbose=False
    )
    r = results[0]
    boxes = r.boxes
    names = r.names

    bboxes = []
    max_conf = 0.0
    if boxes is not None and len(boxes) > 0:
        xyxy = boxes.xyxy.cpu().numpy()
        conf = boxes.conf.cpu().numpy()
        cls = boxes.cls.cpu().numpy().astype(int)
        for (x1, y1, x2, y2), c, k in zip(xyxy, conf, cls):
            label = names.get(k, str(k))
            w, h = int(x2 - x1), int(y2 - y1)
            bboxes.append({
                "x": int(x1),
                "y": int(y1),
                "w": w,
                "h": h,
                "label": label,
                "score": float(c)
            })
            if c > max_conf:
                max_conf = float(c)

    # 검출이 있으면 불량으로 가정 (필요 시 규칙 수정)
    result = "defective" if len(bboxes) > 0 else "normal"

    t1 = time.time()
    return {
        "ok": True,
        "camera_type": camera_type,
        "result": result,
        "defect_score": float(max_conf),
        "classes": [{"name": b["label"], "score": b["score"]} for b in bboxes],
        "bboxes": bboxes,
        "inference_ms": (t1 - t0) * 1000.0,
        "model_version": f"yolo8-{YOLO_WEIGHTS}",
    }

# =========================
# TCP 서버
# =========================
HOST = "127.0.0.1"
PORT = 8008

def recv_all(sock, length: int):
    """정확히 length 바이트 수신 (연결 종료 시 None)"""
    buf = b""
    while len(buf) < length:
        chunk = sock.recv(length - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf

def handle_client(conn: socket.socket, addr):
    print(f"[+] Connected by {addr}")
    try:
        while True:
            # 1바이트 모드코드 미리보기
            peek = conn.recv(1, socket.MSG_PEEK)
            if not peek:
                print("[-] peek empty, closing.")
                break

            # -----------------------
            # 0x01 : Health Check
            # -----------------------
            if peek == b"\x01":
                conn.recv(1)  # 실제로 소비
                conn.sendall(b"OK")
                print("[AI] Health check OK")
                continue

            # -----------------------
            # 0x02 : Dual 분석 (TOP+SIDE)
            # 프로토콜:
            #   [0x02]
            #   [4바이트 TOP length][TOP bytes]
            #   [4바이트 SIDE length][SIDE bytes]
            # 응답: UTF-8 JSON (길이 프레임 없음) 전송 후 연결 종료
            # -----------------------
            if peek == b"\x02":
                conn.recv(1)  # 0x02 소비
                print("[AI] Dual request...")

                # TOP
                len_top_b = recv_all(conn, 4)
                if not len_top_b: break
                len_top = struct.unpack("<I", len_top_b)[0]
                top_bytes = recv_all(conn, len_top)
                if top_bytes is None: break
                img_top = cv2.imdecode(np.frombuffer(top_bytes, np.uint8), cv2.IMREAD_COLOR)

                # SIDE
                len_side_b = recv_all(conn, 4)
                if not len_side_b: break
                len_side = struct.unpack("<I", len_side_b)[0]
                side_bytes = recv_all(conn, len_side)
                if side_bytes is None: break
                img_side = cv2.imdecode(np.frombuffer(side_bytes, np.uint8), cv2.IMREAD_COLOR)

                # YOLO 추론
                res_top  = infer_image(img_top,  "top")
                res_side = infer_image(img_side, "side")

                # C# 파서 호환 JSON
                final_result = {
                    "result":     "정상" if (res_top["result"] == "normal" and res_side["result"] == "normal") else "비정상",
                    "top_result": "정상" if res_top["result"]  == "normal" else "비정상",
                    "side_result":"정상" if res_side["result"] == "normal" else "비정상",
                    "det_top":  [[c["name"], c["score"]] for c in res_top["classes"]],
                    "det_side": [[c["name"], c["score"]] for c in res_side["classes"]],
                }

                json_bytes = json.dumps(final_result, ensure_ascii=False).encode("utf-8")
                conn.sendall(json_bytes)
                print("[AI] Dual done. Sent JSON & closing.")
                break  # 반드시 끊어서 C#이 읽기 종료

            # -----------------------
            # 0x03 : Single 분석
            # 프로토콜:
            #   [0x03]
            #   [4바이트 label length][label utf-8]
            #   [4바이트 image length][image bytes]
            # 응답: UTF-8 JSON 전송 후 연결 종료
            # -----------------------
            if peek == b"\x03":
                conn.recv(1)  # 0x03 소비
                print("[AI] Single request...")

                # label
                len_label_b = recv_all(conn, 4)
                if not len_label_b: break
                len_label = struct.unpack("<I", len_label_b)[0]
                label_b = recv_all(conn, len_label)
                if label_b is None: break
                cam_label = label_b.decode("utf-8") if len_label > 0 else None

                # image
                len_img_b = recv_all(conn, 4)
                if not len_img_b: break
                len_img = struct.unpack("<I", len_img_b)[0]
                img_b = recv_all(conn, len_img)
                if img_b is None: break
                img = cv2.imdecode(np.frombuffer(img_b, np.uint8), cv2.IMREAD_COLOR)

                res = infer_image(img, cam_label)

                # C# 파서 호환 JSON (단일도 한국어 result로 통일)
                final_result = {
                    "result": "정상" if res["result"] == "normal" else "비정상",
                    # 단일일 땐 reason 추출 로직이 빈 문자열로 처리되므로 det_*는 선택
                    ("det_top" if cam_label == "top" else "det_side"):
                        [[c["name"], c["score"]] for c in res["classes"]]
                }

                json_bytes = json.dumps(final_result, ensure_ascii=False).encode("utf-8")
                conn.sendall(json_bytes)
                print("[AI] Single done. Sent JSON & closing.")
                break  # 연결 종료

            # 알 수 없는 코드
            print(f"[AI] Unknown mode byte: {peek!r}. Closing.")
            break

    except Exception as e:
        print(f"[!] Error: {e}")

    finally:
        try:
            conn.shutdown(socket.SHUT_RDWR)
        except:
            pass
        conn.close()
        print(f"[-] Disconnected {addr}")

def main():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind((HOST, PORT))
    s.listen()
    print(f"[AI Server] Listening on {HOST}:{PORT}")
    while True:
        conn, addr = s.accept()
        print(f"[AI Server] Accepted connection from {addr}")
        threading.Thread(target=handle_client, args=(conn, addr), daemon=True).start()

if __name__ == "__main__":
    main()
