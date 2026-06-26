import { useEffect, useRef, useState } from "react";

// Target size for the converted name-card image (portrait).
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

// Draw `img` into the WIDTH×HEIGHT frame with cover-fit (center crop).
function render(canvas: HTMLCanvasElement, img: HTMLImageElement) {
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

export default function App() {
  const [image, setImage] = useState<HTMLImageElement | null>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    if (canvasRef.current && image) render(canvasRef.current, image);
  }, [image]);

  const onPickImage = async (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;
    const dataUrl = await new Promise<string>((resolve) => {
      const reader = new FileReader();
      reader.onload = () => resolve(reader.result as string);
      reader.readAsDataURL(file);
    });
    setImage(await loadImage(dataUrl));
  };

  const onDownload = () => {
    const canvas = canvasRef.current;
    if (!canvas) return;
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

      <div className="controls">
        <input type="file" accept="image/*" onChange={onPickImage} />
        <button type="button" onClick={onDownload} disabled={!image}>
          保存
        </button>
      </div>

      <div className="preview">
        <canvas ref={canvasRef} width={WIDTH} height={HEIGHT} />
      </div>
    </main>
  );
}
