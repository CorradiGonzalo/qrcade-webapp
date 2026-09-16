"use client";

import { useActionState, useRef, useState } from "react";
import { uploadFirmwareAction } from "@/lib/actions/admin";
import { createClient } from "@/lib/supabase/client";

export function UploadFirmwareForm() {
  const [state, formAction, pending] = useActionState(
    uploadFirmwareAction,
    undefined
  );
  const [uploading, setUploading] = useState(false);
  const [uploadError, setUploadError] = useState<string | null>(null);
  const [binUrl, setBinUrl] = useState<string | null>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);

  async function handleFileChange(e: React.ChangeEvent<HTMLInputElement>) {
    const file = e.target.files?.[0];
    if (!file) return;
    setUploading(true);
    setUploadError(null);
    setBinUrl(null);

    const supabase = createClient();
    const path = `${Date.now()}-${file.name}`;
    const { error } = await supabase.storage
      .from("firmware")
      .upload(path, file, { contentType: "application/octet-stream" });

    if (error) {
      setUploadError(
        "No se pudo subir el archivo. ¿Existe el bucket 'firmware' en Supabase Storage?"
      );
      setUploading(false);
      return;
    }

    const { data } = supabase.storage.from("firmware").getPublicUrl(path);
    setBinUrl(data.publicUrl);
    setUploading(false);
  }

  return (
    <div className="rounded-2xl border border-line bg-surface p-6">
      <div className="mb-1 text-sm font-bold">Subir nueva versión</div>
      <p className="mb-4 text-xs text-ink-muted">
        Queda disponible para todas las máquinas reclamadas — cada dueño
        decide cuándo instalarla, vos no forzás nada.
      </p>

      <form action={formAction} className="space-y-3">
        <div>
          <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
            Versión
          </label>
          <input
            name="version"
            required
            placeholder="ej: v4"
            className="w-full rounded-lg border border-line-soft bg-surface-2 px-3.5 py-2.5 text-sm outline-none focus:border-brand"
          />
        </div>

        <div>
          <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
            Archivo .bin
          </label>
          <input
            ref={fileInputRef}
            type="file"
            accept=".bin"
            onChange={handleFileChange}
            className="w-full text-xs text-ink-muted file:mr-3 file:rounded-lg file:border-0 file:bg-surface-2 file:px-3 file:py-2 file:text-xs file:font-semibold file:text-ink"
          />
          <input type="hidden" name="bin_url" value={binUrl ?? ""} />
          {uploading && (
            <p className="mt-1 text-[11px] text-info">Subiendo…</p>
          )}
          {uploadError && (
            <p className="mt-1 text-[11px] text-danger">{uploadError}</p>
          )}
          {binUrl && !uploading && (
            <p className="mt-1 text-[11px] text-ok">Archivo listo ✓</p>
          )}
        </div>

        <div>
          <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
            Changelog
          </label>
          <textarea
            name="changelog"
            rows={2}
            placeholder="Qué cambia en esta versión"
            className="w-full rounded-lg border border-line-soft bg-surface-2 px-3.5 py-2.5 text-sm outline-none focus:border-brand"
          />
        </div>

        {state?.error && (
          <p className="text-xs font-medium text-danger">{state.error}</p>
        )}
        {state?.ok && (
          <p className="text-xs font-medium text-ok">
            Versión publicada — se notificó a todas las máquinas reclamadas.
          </p>
        )}

        <button
          type="submit"
          disabled={pending || uploading || !binUrl}
          className="rounded-lg bg-brand px-4 py-2.5 text-sm font-semibold text-white transition hover:opacity-90 disabled:opacity-50"
        >
          {pending ? "Publicando…" : "Publicar versión"}
        </button>
      </form>
    </div>
  );
}
