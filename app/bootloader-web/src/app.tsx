import { useState, useEffect } from 'react';
import { SharedAxis, SharedAxisTransform } from "material-design-transform";
import './app.css';

export function App() {
  const [fileInputContent, setFileInputContent] = useState("Current File: None");
  const [statusTipContent, setStatusTipContent] = useState("");
  const [statusTipColor, setStatusTipColor] = useState<string | undefined>(undefined);
  const [isUploading, setIsUploading] = useState(false);

  const [wsId, setWsId] = useState(0);
  const [ws, setWs] = useState<WebSocket | null>(null);
  useEffect(() => {
    const is_dev = import.meta.env.MODE == "development";
    const url = is_dev ? "ws://192.168.7.1/ws" : "/ws";
    const ws = new WebSocket(url);
    function closeWs() {
      switch (ws.readyState) {
        case ws.CLOSING:
        case ws.CLOSED:
          break;
        default:
          ws.close();
      }
    }
    let retryTimer: number | undefined;

    ws.onopen = () => {
      setWs(ws);
    };

    ws.onmessage = (e) => {
      // @TODO:
      console.log(e.data);
    };

    ws.onerror = (e) => {
      console.error(e);
      closeWs();
    };

    let closed = false;

    function onclose() {
      if (closed) return;
      setWs(null);
      retryTimer = setTimeout(() => {
        setWsId(v => v + 1);
      }, 1000);
    }

    switch (ws.readyState) {
      case ws.CLOSING:
      case ws.CLOSED:
        onclose();
        break;
      default:
        ws.onclose = onclose;
        break;
    }
    return () => {
      closed = true;
      clearTimeout(retryTimer);
      closeWs();
      setWs(null);
    };
  }, [wsId]);

  return (
    <SharedAxis transform={SharedAxisTransform.fromTopToBottom} class="upload-container" keyId={ws}>
      {ws == null ? <>Connecting...</> :
        <>
          <h1>Bootloader</h1>
          <div style={{ minHeight: "32px" }} />
          <button class="upload-btn"
            disabled={isUploading}
            onClick={() => {
              const input = document.createElement("input");
              input.type = "file";
              input.name = "upload_file";
              input.onchange = async () => {
                const selectedFile = input.files?.[0];
                if (!selectedFile) {
                  setFileInputContent("Current File: None");
                  setStatusTipContent("");
                  setStatusTipColor(undefined);
                  return;
                }

                setFileInputContent(`${selectedFile.name} (${(selectedFile.size / 1024).toFixed(2)} KB)`);
                setStatusTipContent("Uploading...");

                try {
                  setIsUploading(true);
                  await uploadFileWithProgress(selectedFile, "/upload",
                    (progress) => {
                      const p = Math.round(progress * 100);
                      if (p == 100) {
                        setStatusTipContent("⟳ Processing...");
                      } else {
                        setStatusTipContent(`📁⇄📁 Uploading... (${p}%)`);
                      }
                      setStatusTipColor(undefined);
                    });
                  setStatusTipContent("✅ Upload succeeded");
                  setStatusTipColor("#10b981");
                } catch (err) {
                  setStatusTipContent(`⚠️ Upload error: ${(err as Error).message}`);
                  setStatusTipColor("#f59e0b");
                }
                setIsUploading(false);
              };
              input.click();
            }}>🛠️ Select IMG to upload</button>

          <div class="file-info" id="fileInfo">{fileInputContent}</div>
          <div class="status-tip" id="statusTip" style={{ color: statusTipColor }}>{statusTipContent}</div>

          <div style={{ minHeight: "32px" }} />
          <button class="upload-btn"
            disabled={isUploading && ws !== null}
            onClick={() => {
              if (ws !== null) {
                ws.send("exit");
                ws.close();
              }
            }}>⏻ Exit bootloader</button>

          <div style={{ minHeight: "16px" }} />
          <div class="status-tip" id="statusTip">@JohnGu https://github.com/JohnGu9</div>

        </>
      }
    </SharedAxis>
  );
}


function uploadFileWithProgress(file: File, url: string, onprogress: (percentComplete: number) => unknown) {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();

    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable) {
        const percentComplete = e.loaded / e.total;
        onprogress(percentComplete);
      }
    };

    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve(null);
      } else {
        reject(new Error(`status code ${xhr.status}`));
      }
    };

    xhr.onerror = () => {
      reject(new Error('Network error occurred'));
    };

    xhr.open('POST', url, true);
    xhr.send(file);
  });
}
