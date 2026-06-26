import { useEffect, useRef, useState } from "react";

// Target size for the converted display image (portrait).
const WIDTH = 540;
const HEIGHT = 960;

function loadImage(src: string): Promise<HTMLImageElement> {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => resolve(img);
    img.onerror = reject;
    img.src = src;
  });
}

function readImage(
  e: React.ChangeEvent<HTMLInputElement>,
): Promise<HTMLImageElement | null> {
  const file = e.target.files?.[0];
  if (!file) return Promise.resolve(null);
  return new Promise((resolve) => {
    const reader = new FileReader();
    reader.onload = () => resolve(loadImage(reader.result as string));
    reader.readAsDataURL(file);
  });
}

// Draw `img` into the WIDTH×HEIGHT frame with cover-fit (center crop).
function renderDisplay(canvas: HTMLCanvasElement, img: HTMLImageElement) {
  canvas.width = WIDTH;
  canvas.height = HEIGHT;
  const ctx = canvas.getContext("2d")!;
  ctx.fillStyle = "#ffffff";
  ctx.fillRect(0, 0, WIDTH, HEIGHT);

  const scale = Math.max(WIDTH / img.width, HEIGHT / img.height);
  const sw = WIDTH / scale;
  const sh = HEIGHT / scale;
  ctx.drawImage(
    img,
    (img.width - sw) / 2,
    (img.height - sh) / 2,
    sw,
    sh,
    0,
    0,
    WIDTH,
    HEIGHT,
  );
}

function ImageField({
  label,
  image,
  onChange,
}: {
  label: string;
  image: HTMLImageElement | null;
  onChange: (img: HTMLImageElement | null) => void;
}) {
  return (
    <label className="field">
      <span>{label}</span>
      <input
        type="file"
        accept="image/*"
        onChange={async (e) => onChange(await readImage(e))}
      />
      {image && <img className="thumb" src={image.src} alt="" />}
    </label>
  );
}

export default function App() {
  const [name, setName] = useState("");
  const [url, setUrl] = useState("");
  const [displayImage, setDisplayImage] = useState<HTMLImageElement | null>(
    null,
  );
  const [shareImage1, setShareImage1] = useState<HTMLImageElement | null>(null);
  const [shareImage2, setShareImage2] = useState<HTMLImageElement | null>(null);
  const [shareMessage, setShareMessage] = useState("");
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    if (canvasRef.current && displayImage)
      renderDisplay(canvasRef.current, displayImage);
  }, [displayImage]);

  const onSave = () => {
    const canvas = canvasRef.current;
    if (!canvas || !displayImage) return;
    canvas.toBlob((blob) => {
      if (!blob) return;
      const a = document.createElement("a");
      a.href = URL.createObjectURL(blob);
      a.download = "namecard.png";
      a.click();
      URL.revokeObjectURL(a.href);
    }, "image/png");
  };

  return (
    <main>
      <header>
        <h1>NameCardKnot Editor</h1>
      </header>

      <div className="form">
        <label className="field">
          <span>名前</span>
          <input
            type="text"
            value={name}
            onChange={(e) => setName(e.target.value)}
          />
        </label>

        <label className="field">
          <span>URL</span>
          <input
            type="text"
            value={url}
            onChange={(e) => setUrl(e.target.value)}
            placeholder="https://example.com"
          />
        </label>

        <div className="field">
          <span>表示画像</span>
          <input
            type="file"
            accept="image/*"
            onChange={async (e) => setDisplayImage(await readImage(e))}
          />
          {displayImage && (
            <canvas className="preview" ref={canvasRef} width={WIDTH} height={HEIGHT} />
          )}
        </div>

        <ImageField
          label="共有画像 1"
          image={shareImage1}
          onChange={setShareImage1}
        />
        <ImageField
          label="共有画像 2"
          image={shareImage2}
          onChange={setShareImage2}
        />

        <label className="field">
          <span>共有メッセージ</span>
          <textarea
            value={shareMessage}
            rows={4}
            onChange={(e) => setShareMessage(e.target.value)}
          />
        </label>

        <button type="button" onClick={onSave} disabled={!displayImage}>
          保存
        </button>
      </div>
    </main>
  );
}
