"use client";

import { useRef, useState, useTransition } from "react";
import { useRouter, usePathname, useSearchParams } from "next/navigation";
import {
  createFolderAction,
  renameFolderAction,
  deleteFolderAction,
} from "@/lib/actions/folders";
import {
  groupFoldersByParent,
  folderPath,
  countDevicesDeep,
} from "@/lib/folders";
import type { DeviceFolder } from "@/lib/supabase/types";

/**
 * Navegador de carpetas estilo "explorador de archivos": breadcrumb +
 * tiles de subcarpetas del nivel actual, y debajo (fuera de este
 * componente) el caller dibuja las máquinas que están en este nivel.
 * Todo el estado de navegación vive en la URL (?carpeta=<id>) — así se
 * puede linkear/refrescar sin perder el lugar, mismo patrón que
 * SalesPeriodFilter.
 */
export function FolderBrowser({
  folders,
  deviceCountByFolder,
}: {
  folders: DeviceFolder[];
  /** Todas las máquinas visibles en este contexto (propias, o todas si
   * sos admin) — sólo se usa para el contador recursivo de cada tile. */
  deviceCountByFolder: { folder_id: string | null }[];
}) {
  const router = useRouter();
  const pathname = usePathname();
  const searchParams = useSearchParams();
  const currentFolderId = searchParams.get("carpeta");

  const byParent = groupFoldersByParent(folders);
  const subfolders = byParent.get(currentFolderId) ?? [];
  const breadcrumb = folderPath(folders, currentFolderId);

  function irA(folderId: string | null) {
    if (folderId) router.push(`${pathname}?carpeta=${folderId}`, { scroll: false });
    else router.push(pathname, { scroll: false });
  }

  return (
    <div className="mb-5">
      <div className="mb-3 flex flex-wrap items-center gap-1 text-[13px]">
        <button
          onClick={() => irA(null)}
          className={`font-semibold hover:text-brand ${
            !currentFolderId ? "text-ink" : "text-ink-muted"
          }`}
        >
          Inicio
        </button>
        {breadcrumb.map((f) => (
          <span key={f.id} className="flex items-center gap-1">
            <span className="text-ink-faint">/</span>
            <button
              onClick={() => irA(f.id)}
              className={`font-semibold hover:text-brand ${
                f.id === currentFolderId ? "text-ink" : "text-ink-muted"
              }`}
            >
              {f.name}
            </button>
          </span>
        ))}
      </div>

      <div className="flex flex-wrap items-stretch gap-2.5">
        {subfolders.map((f) => (
          <FolderTile
            key={f.id}
            folder={f}
            count={countDevicesDeep(folders, deviceCountByFolder, f.id)}
            onOpen={() => irA(f.id)}
          />
        ))}
        <NewFolderTile parentId={currentFolderId} />
      </div>
    </div>
  );
}

function FolderTile({
  folder,
  count,
  onOpen,
}: {
  folder: DeviceFolder;
  count: number;
  onOpen: () => void;
}) {
  const [menuOpen, setMenuOpen] = useState(false);
  const [renaming, setRenaming] = useState(false);
  const [name, setName] = useState(folder.name);
  const [pending, startTransition] = useTransition();
  const [error, setError] = useState<string | null>(null);

  function guardarNombre() {
    const clean = name.trim();
    if (!clean || clean === folder.name) {
      setRenaming(false);
      setName(folder.name);
      return;
    }
    startTransition(async () => {
      try {
        await renameFolderAction(folder.id, clean);
        setRenaming(false);
      } catch (err) {
        setError(err instanceof Error ? err.message : "No se pudo renombrar.");
      }
    });
  }

  function borrar() {
    if (
      !window.confirm(
        `¿Borrar "${folder.name}"? Las subcarpetas también se borran. Las máquinas que estaban adentro no se tocan — quedan sin carpeta.`
      )
    )
      return;
    startTransition(() => deleteFolderAction(folder.id));
  }

  if (renaming) {
    return (
      <div className="flex min-w-[180px] items-center gap-1.5 rounded-xl border border-brand bg-surface p-3.5">
        <input
          autoFocus
          value={name}
          onChange={(e) => setName(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === "Enter") guardarNombre();
            if (e.key === "Escape") {
              setRenaming(false);
              setName(folder.name);
            }
          }}
          maxLength={60}
          className="w-full min-w-0 bg-transparent text-sm font-semibold text-ink outline-none"
        />
        <button
          disabled={pending}
          onClick={guardarNombre}
          className="shrink-0 text-xs font-semibold text-brand"
        >
          ✓
        </button>
      </div>
    );
  }

  return (
    <div className="group relative min-w-[150px] rounded-xl border border-line bg-surface p-3.5 transition hover:border-line-soft">
      <button onClick={onOpen} className="block w-full text-left">
        <div className="mb-1 text-xl">📁</div>
        <div className="truncate pr-5 text-sm font-semibold text-ink">{folder.name}</div>
        <div className="text-[11px] text-ink-faint">
          {count} máquina{count === 1 ? "" : "s"}
        </div>
      </button>

      <button
        onClick={() => setMenuOpen((v) => !v)}
        className="absolute right-2 top-2 rounded-md p-1 text-ink-faint opacity-0 transition hover:bg-surface-2 hover:text-ink group-hover:opacity-100"
      >
        ⋯
      </button>

      {menuOpen && (
        <>
          <div className="fixed inset-0 z-10" onClick={() => setMenuOpen(false)} />
          <div className="absolute right-2 top-8 z-20 w-36 rounded-lg border border-line-soft bg-surface p-1 shadow-lg">
            <button
              onClick={() => {
                setMenuOpen(false);
                setRenaming(true);
              }}
              className="block w-full rounded-md px-2.5 py-1.5 text-left text-xs font-medium text-ink-muted hover:bg-surface-2"
            >
              Renombrar
            </button>
            <button
              onClick={() => {
                setMenuOpen(false);
                borrar();
              }}
              className="block w-full rounded-md px-2.5 py-1.5 text-left text-xs font-medium text-danger hover:bg-surface-2"
            >
              Borrar
            </button>
          </div>
        </>
      )}
      {error && <p className="mt-1 text-[11px] font-medium text-danger">{error}</p>}
    </div>
  );
}

function NewFolderTile({ parentId }: { parentId: string | null }) {
  const [open, setOpen] = useState(false);
  const [pending, startTransition] = useTransition();
  const [error, setError] = useState<string | null>(null);
  const inputRef = useRef<HTMLInputElement>(null);

  function crear(formData: FormData) {
    setError(null);
    startTransition(async () => {
      const res = await createFolderAction(undefined, formData);
      if (res?.error) setError(res.error);
      else setOpen(false);
    });
  }

  if (!open) {
    return (
      <button
        onClick={() => {
          setOpen(true);
          setTimeout(() => inputRef.current?.focus(), 0);
        }}
        className="flex min-w-[150px] flex-col items-center justify-center rounded-xl border border-dashed border-line-soft p-3.5 text-ink-faint transition hover:border-brand hover:text-brand"
      >
        <div className="mb-1 text-xl">+</div>
        <div className="text-xs font-semibold">Nueva carpeta</div>
      </button>
    );
  }

  return (
    <form action={crear} className="min-w-[180px] rounded-xl border border-brand bg-surface p-3.5">
      <input type="hidden" name="parent_id" value={parentId ?? ""} />
      <input
        ref={inputRef}
        name="name"
        maxLength={60}
        placeholder="Nombre de la carpeta"
        onKeyDown={(e) => e.key === "Escape" && setOpen(false)}
        className="w-full bg-transparent text-sm font-semibold text-ink outline-none placeholder:text-ink-faint placeholder:font-normal"
      />
      <div className="mt-2 flex gap-2">
        <button
          type="submit"
          disabled={pending}
          className="rounded-md bg-brand px-2.5 py-1 text-[11px] font-semibold text-white disabled:opacity-50"
        >
          {pending ? "Creando…" : "Crear"}
        </button>
        <button
          type="button"
          onClick={() => setOpen(false)}
          className="rounded-md border border-line-soft px-2.5 py-1 text-[11px] font-semibold text-ink-muted"
        >
          Cancelar
        </button>
      </div>
      {error && <p className="mt-1.5 text-[11px] font-medium text-danger">{error}</p>}
    </form>
  );
}
