"use client";

import { useRef, useState, useTransition } from "react";
import { moveDeviceToFolderAction } from "@/lib/actions/folders";
import { folderFullName } from "@/lib/folders";
import type { DeviceFolder } from "@/lib/supabase/types";

export function MoveToFolderMenu({
  deviceId,
  currentFolderId,
  folders,
}: {
  deviceId: string;
  currentFolderId: string | null;
  folders: DeviceFolder[];
}) {
  const [open, setOpen] = useState(false);
  const [pending, startTransition] = useTransition();
  const [error, setError] = useState<string | null>(null);
  const menuRef = useRef<HTMLDivElement>(null);

  function mover(folderId: string | null) {
    setError(null);
    startTransition(async () => {
      try {
        await moveDeviceToFolderAction(deviceId, folderId);
        setOpen(false);
      } catch (err) {
        setError(err instanceof Error ? err.message : "No se pudo mover.");
      }
    });
  }

  return (
    <div className="relative" ref={menuRef}>
      <button
        type="button"
        disabled={pending}
        onClick={() => setOpen((v) => !v)}
        title="Mover a carpeta"
        className="rounded-md bg-surface-2/90 p-1.5 text-ink-faint backdrop-blur transition hover:bg-surface-2 hover:text-ink disabled:opacity-50"
      >
        📁
      </button>

      {open && (
        <>
          <div className="fixed inset-0 z-10" onClick={() => setOpen(false)} />
          <div className="absolute right-0 z-20 mt-1.5 max-h-64 w-56 overflow-y-auto rounded-lg border border-line-soft bg-surface p-1.5 shadow-lg">
            <button
              type="button"
              onClick={() => mover(null)}
              className={`block w-full rounded-md px-2.5 py-1.5 text-left text-xs font-medium hover:bg-surface-2 ${
                currentFolderId === null ? "text-brand" : "text-ink-muted"
              }`}
            >
              Sin carpeta
            </button>
            {folders.length === 0 && (
              <p className="px-2.5 py-1.5 text-[11px] text-ink-faint">
                Todavía no creaste ninguna carpeta.
              </p>
            )}
            {folders.map((f) => (
              <button
                key={f.id}
                type="button"
                onClick={() => mover(f.id)}
                className={`block w-full truncate rounded-md px-2.5 py-1.5 text-left text-xs font-medium hover:bg-surface-2 ${
                  currentFolderId === f.id ? "text-brand" : "text-ink-muted"
                }`}
                title={folderFullName(folders, f.id)}
              >
                {folderFullName(folders, f.id)}
              </button>
            ))}
            {error && (
              <p className="px-2.5 py-1 text-[11px] font-medium text-danger">{error}</p>
            )}
          </div>
        </>
      )}
    </div>
  );
}
