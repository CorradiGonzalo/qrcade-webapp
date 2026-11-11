"use client";

import { useRef, useState, useTransition } from "react";
import { createClient } from "@/lib/supabase/client";
import { saveLogoNexoAction } from "@/lib/actions/admin";
import { imageFileToLogoBin } from "@/lib/image-to-rgb565";

/**
 * Logo "NexoQR" (LOGO2.BIN) — GLOBAL, uno solo para toda la plataforma.
 * Lo sube un admin desde acá y TODAS las pantallas de TODAS las
 * cafeteras (de cualquier dueño) lo toman de ahí en su próximo chequeo
 * — a diferencia del logo del cliente, que es por-máquina (ver
 * LogoClienteUploadForm en /dashboard/maquinas/[id]). Misma conversión
 * en el browser y mismo bucket "firmware" (carpeta logos/nexo/).
 */
export function UploadLogoNexoForm() {
  const [pending, startTransition] = useTransition();
  const [uploading, setUploading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [ok, setOk] = useState(false);
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
      const path = `logos/nexo/${Date.now()}.bin`;
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
          await saveLogoNexoAction(data.publicUrl);
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
    <div className="rounded-2xl border border-line bg-surface p-6">
      <div className="mb-1 text-sm font-bold">Logo NexoQR (global)</div>
      <p className="mb-4 text-xs text-ink-muted">
        Uno solo para toda la plataforma — lo toman TODAS las pantallas de
        TODAS las cafeteras, sin importar el dueño. Subí una imagen (PNG
        o JPG), la convertimos automáticamente al formato que necesita la
        pantalla.
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
          Logo guardado ✓ — se actualiza solo en todas las pantallas
          (requiere firmware de pantalla v2.5.0 o más nuevo).
        </p>
      )}
    </div>
  );
}
