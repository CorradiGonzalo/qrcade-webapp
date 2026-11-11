"use client";

import { useRef, useState, useTransition } from "react";
import { createClient } from "@/lib/supabase/client";
import { saveDeviceLogoClienteAction } from "@/lib/actions/devices";
import { imageFileToLogoBin } from "@/lib/image-to-rgb565";
import type { Device } from "@/lib/supabase/types";

/**
 * Logo "del cliente" (LOGO1.BIN) de ESTA máquina — lo sube el dueño
 * desde acá en vez de tener que copiarlo a mano a la microSD. Convierte
 * la imagen en el browser (320x480 RGB565 crudo, igual al formato que
 * ya usa el firmware) y la sube al mismo bucket público "firmware" que
 * ya existe para las versiones de firmware (carpeta logos/cliente/,
 * nunca pisa nada de firmware/*.bin). No toca el firmware de la placa:
 * la pantalla recién la va a descargar sola cuando tenga la función
 * nueva (v2.5.0+) — hasta entonces esto sólo queda guardado.
 */
export function LogoClienteUploadForm({ device }: { device: Device }) {
  const [pending, startTransition] = useTransition();
  const [uploading, setUploading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [ok, setOk] = useState(false);
  const [url, setUrl] = useState(device.display_logo_cliente_url);
  const fileInputRef = useRef<HTMLInputElement>(null);

  async function handleFileChange(e: React.ChangeEvent<HTMLInputElement>) {
    const file = e.target.files?.[0];
    if (!file) return;
    setUploading(true);
    setError(null);
    setOk(false);

    try {
      const bin = await imageFileToLogoBin(file);
      const supabase = createClient();
      const path = `logos/cliente/${device.id}-${Date.now()}.bin`;
      const { error: uploadError } = await supabase.storage
        .from("firmware")
        .upload(path, bin, { contentType: "application/octet-stream" });

      if (uploadError) {
        setError(
          "No se pudo subir el archivo. ¿Existe el bucket 'firmware' en Supabase Storage?"
        );
        setUploading(false);
        return;
      }

      const { data } = supabase.storage.from("firmware").getPublicUrl(path);
      setUploading(false);

      startTransition(async () => {
        try {
          await saveDeviceLogoClienteAction(device.id, data.publicUrl);
          setUrl(data.publicUrl);
          setOk(true);
        } catch (err) {
          setError(err instanceof Error ? err.message : "No se pudo guardar el logo.");
        }
      });
    } catch {
      setError("No se pudo convertir la imagen. Probá con otro archivo (PNG o JPG).");
      setUploading(false);
    } finally {
      if (fileInputRef.current) fileInputRef.current.value = "";
    }
  }

  return (
    <div className="rounded-2xl border border-line bg-surface p-5">
      <div className="mb-1 text-sm font-bold">Logo del cliente (pantalla)</div>
      <p className="mb-3 text-xs text-ink-muted">
        Se muestra en la pantalla de esta máquina, intercalado con el logo
        de NexoQR. Subí una imagen (PNG o JPG) — la convertimos
        automáticamente al formato que necesita la pantalla.
      </p>

      <input
        ref={fileInputRef}
        type="file"
        accept="image/png,image/jpeg"
        onChange={handleFileChange}
        className="w-full text-xs text-ink-muted file:mr-3 file:rounded-lg file:border-0 file:bg-surface-2 file:px-3 file:py-2 file:text-xs file:font-semibold file:text-ink"
      />

      {uploading && (
        <p className="mt-2 text-[11px] text-info">Convirtiendo y subiendo…</p>
      )}
      {pending && !uploading && (
        <p className="mt-2 text-[11px] text-info">Guardando…</p>
      )}
      {error && <p className="mt-2 text-[11px] text-danger">{error}</p>}
      {ok && !pending && (
        <p className="mt-2 text-[11px] text-ok">
          Logo guardado ✓ — se actualiza en la pantalla sola en el próximo
          chequeo (requiere firmware de pantalla v2.5.0 o más nuevo).
        </p>
      )}
      {url && !ok && !uploading && (
        <p className="mt-2 text-[11px] text-ink-faint">
          Ya hay un logo cargado para esta máquina.
        </p>
      )}
    </div>
  );
}
