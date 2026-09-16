"use client";

import { useActionState, useEffect, useRef } from "react";
import { useRouter } from "next/navigation";
import { claimDeviceAction } from "@/lib/actions/devices";

const BOXES = 6;

export function ClaimDeviceForm() {
  const [state, formAction, pending] = useActionState(
    claimDeviceAction,
    undefined
  );
  const router = useRouter();
  const hiddenInputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    if (state?.ok) router.push("/dashboard");
  }, [state?.ok, router]);

  function handleBoxInput(e: React.FormEvent<HTMLInputElement>, index: number) {
    const target = e.currentTarget;
    const form = target.form!;
    const boxes = Array.from(
      form.querySelectorAll<HTMLInputElement>("[data-code-box]")
    );
    target.value = target.value.toUpperCase().slice(-1);
    if (target.value && index < BOXES - 1) boxes[index + 1]?.focus();
    if (hiddenInputRef.current) {
      hiddenInputRef.current.value = boxes.map((b) => b.value).join("");
    }
  }

  return (
    <form action={formAction} className="rounded-2xl border border-line bg-surface p-7">
      <input ref={hiddenInputRef} type="hidden" name="code" />

      <label className="mb-2.5 block text-center text-xs font-semibold text-ink-muted">
        Código de la máquina
      </label>
      <div className="mb-[22px] flex justify-center gap-2.5">
        {Array.from({ length: BOXES }).map((_, i) => (
          <input
            key={i}
            data-code-box
            maxLength={1}
            onInput={(e) => handleBoxInput(e, i)}
            className="h-[60px] w-[52px] rounded-[10px] border border-line-soft bg-surface-2 text-center font-mono text-2xl font-bold text-ink outline-none focus:border-brand"
          />
        ))}
      </div>

      <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
        Alias de la máquina (opcional)
      </label>
      <input
        name="alias"
        placeholder="Ej: Peluchera Shopping Norte"
        className="mb-[22px] w-full rounded-lg border border-line-soft bg-surface-2 px-3.5 py-3 text-sm text-ink outline-none focus:border-brand"
      />

      {state?.error && (
        <p className="mb-3 text-center text-xs font-medium text-danger">
          {state.error}
        </p>
      )}

      <button
        type="submit"
        disabled={pending}
        className="w-full rounded-lg bg-brand py-3 text-sm font-semibold text-white shadow-lg shadow-brand/30 transition hover:opacity-90 disabled:opacity-60"
      >
        {pending ? "Vinculando…" : "Vincular a mi cuenta"}
      </button>
    </form>
  );
}
