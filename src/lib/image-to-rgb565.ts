/**
 * Convierte una imagen (PNG/JPG elegida por el usuario) al formato RAW
 * que espera la pantalla de la cafetera: 320x480 RGB565, big-endian
 * (byte alto primero), sin ningún header — exactamente lo que hoy se
 * copiaba a mano a la microSD como LOGO1.BIN/LOGO2.BIN. Corre 100% en
 * el browser (canvas), nunca en el servidor — así no hace falta ninguna
 * librería de imágenes en el runtime de Cloudflare Workers.
 *
 * La imagen se dibuja en modo "contain" (se ve completa, sin recortar)
 * centrada sobre un fondo del navy de marca — mismo criterio que ya
 * usa la pantalla para las franjas (#16233b, ver firmware).
 */

const ANCHO_LOGO = 320;
const ALTO_LOGO = 480;
const FONDO_NAVY = "#16233b";

export async function imageFileToLogoBin(file: File): Promise<Blob> {
  const bitmap = await cargarImagen(file);

  const canvas = document.createElement("canvas");
  canvas.width = ANCHO_LOGO;
  canvas.height = ALTO_LOGO;
  const ctx = canvas.getContext("2d", { willReadFrequently: true });
  if (!ctx) throw new Error("No se pudo preparar el canvas de conversión.");

  ctx.fillStyle = FONDO_NAVY;
  ctx.fillRect(0, 0, ANCHO_LOGO, ALTO_LOGO);

  const escala = Math.min(
    ANCHO_LOGO / bitmap.width,
    ALTO_LOGO / bitmap.height
  );
  const w = bitmap.width * escala;
  const h = bitmap.height * escala;
  const x = (ANCHO_LOGO - w) / 2;
  const y = (ALTO_LOGO - h) / 2;
  ctx.drawImage(bitmap, x, y, w, h);

  const { data } = ctx.getImageData(0, 0, ANCHO_LOGO, ALTO_LOGO);
  const out = new Uint8Array(ANCHO_LOGO * ALTO_LOGO * 2);

  for (let px = 0, i = 0; px < ANCHO_LOGO * ALTO_LOGO; px++, i += 4) {
    const r = data[i];
    const g = data[i + 1];
    const b = data[i + 2];
    const rgb565 =
      ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    // Big-endian: byte alto primero, igual al script de conversión que
    // ya usa el firmware para los LOGO*.BIN existentes.
    out[px * 2] = (rgb565 >> 8) & 0xff;
    out[px * 2 + 1] = rgb565 & 0xff;
  }

  if ("close" in bitmap) bitmap.close();

  return new Blob([out], { type: "application/octet-stream" });
}

async function cargarImagen(file: File): Promise<ImageBitmap | HTMLImageElement> {
  if (typeof createImageBitmap === "function") {
    try {
      return await createImageBitmap(file);
    } catch {
      // Sigue al fallback de abajo (algunos formatos/navegadores viejos
      // no soportan createImageBitmap para todos los tipos de archivo).
    }
  }

  return new Promise((resolve, reject) => {
    const img = new Image();
    const url = URL.createObjectURL(file);
    img.onload = () => {
      URL.revokeObjectURL(url);
      resolve(img);
    };
    img.onerror = () => {
      URL.revokeObjectURL(url);
      reject(new Error("No se pudo leer la imagen."));
    };
    img.src = url;
  });
}
