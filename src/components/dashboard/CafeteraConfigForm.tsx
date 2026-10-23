"use client";

import { useActionState, useState, useTransition } from "react";
import {
  requestCleanCycleAction,
  saveCafeteraConfigAction,
} from "@/lib/actions/devices";
import type { Device } from "@/lib/supabase/types";

/**
 * Config operativa de una cafetera (ver migración 0011) — todo lo que
 * el firmware "ESP32 MPAY" original pedía a mano por el portal cautivo,
 * salvo el Access Token/collector_id de MP (esos se cargan una sola vez
 * en "Mi cuenta" y se reusan para todas las máquinas) y el WiFi (eso sí
 * sigue siendo directo desde la placa, no hay forma de evitarlo en el
 * primer alta).
 */
export function CafeteraConfigForm({
  device,
  hasAccountToken,
}: {
  device: Device;
  hasAccountToken: boolean;
}) {
  const [state, formAction, pending] = useActionState(
    saveCafeteraConfigAction,
    undefined
  );
  const [pulsoAlto, setPulsoAlto] = useState(device.cafe_pulso_nivel_high);
  const [limpiezaPendiente, startLimpieza] = useTransition();
  const [limpiezaOk, setLimpiezaOk] = useState(false);

  function dispararLimpieza() {
    startLimpieza(async () => {
      await requestCleanCycleAction(device.id);
      setLimpiezaOk(true);
      setTimeout(() => setLimpiezaOk(false), 4000);
    });
  }

  return (
    <div className="space-y-5">
      {!hasAccountToken && (
        <div className="rounded-lg border border-warn bg-warn-bg px-4 py-4 text-xs text-warn">
          ⚠ Todavía no cargaste el Access Token de tu cuenta en{" "}
          <a href="/dashboard/cuenta" className="font-semibold underline">
            Mi cuenta
          </a>
          . Esta cafetera no va a poder detectar pagos hasta que lo cargues
          — es la misma cuenta que usan las garras.
        </div>
      )}

      <div className="rounded-2xl border border-line bg-surface p-6">
        <div className="mb-1 text-sm font-bold">Identificador de pago (MP)</div>
        <p className="mb-3 text-xs text-ink-muted">
          Esto es lo que distingue los pagos de ESTA cafetera de los de
          otra máquina de la misma cuenta. Se generó solo a partir de la
          MAC — si esta placa venía con el firmware viejo y ya tenía
          pagos andando con otro external_reference, hay que actualizar
          la config de cobro de Mercado Pago de esta máquina puntual para
          que apunte a este valor.
        </p>
        <div className="flex items-center gap-2 rounded-lg border border-line-soft bg-surface-2 px-3.5 py-3">
          <code className="flex-1 overflow-x-auto text-xs font-semibold text-ink">
            {device.cafe_mp_external_reference}
          </code>
        </div>
      </div>

      <div className="rounded-2xl border border-line bg-surface p-6">
        <div className="mb-1 text-sm font-bold">Limpieza</div>
        <p className="mb-4 text-xs text-ink-muted">
          2 pulsos de relé separados por 20s. Se dispara sola cada tantos
          créditos usados, o la podés pedir ahora mismo.
        </p>

        <form action={formAction} className="space-y-4">
          <input type="hidden" name="device_id" value={device.id} />
          <input
            type="hidden"
            name="cafe_pulso_nivel_high"
            value={pulsoAlto ? "on" : "off"}
          />

          <div>
            <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
              Limpiar cada X ciclos
            </label>
            <input
              name="cafe_clean_every_n_cycles"
              type="number"
              min="1"
              step="1"
              defaultValue={device.cafe_clean_every_n_cycles}
              className="w-full rounded-lg border border-line-soft bg-surface-2 px-3.5 py-3 text-sm text-ink outline-none focus:border-brand"
            />
          </div>

          <div>
            <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
              Duración de cada pulso de limpieza (ms)
            </label>
            <input
              name="cafe_clean_pulse_ms"
              type="number"
              min="100"
              step="1"
              defaultValue={device.cafe_clean_pulse_ms}
              className="w-full rounded-lg border border-line-soft bg-surface-2 px-3.5 py-3 text-sm text-ink outline-none focus:border-brand"
            />
          </div>

          <details className="rounded-lg border border-line-soft bg-surface-2 px-3.5 py-3">
            <summary className="cursor-pointer text-xs font-semibold text-ink-muted">
              Avanzado (pulso / relé / calentador / reset BT)
            </summary>
            <div className="mt-3.5 space-y-4">
              <div>
                <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
                  Pulso mínimo en GPIO27 (µs)
                </label>
                <input
                  name="cafe_pulso_min_us"
                  type="number"
                  min="1000"
                  step="1"
                  defaultValue={device.cafe_pulso_min_us}
                  className="w-full rounded-lg border border-line bg-surface px-3.5 py-2.5 text-sm text-ink outline-none focus:border-brand"
                />
              </div>

              <div className="flex gap-2">
                <button
                  type="button"
                  onClick={() => setPulsoAlto(true)}
                  className={`flex-1 rounded-lg border px-3 py-2 text-xs font-semibold transition ${
                    pulsoAlto
                      ? "border-brand bg-brand-soft text-ink"
                      : "border-line-soft bg-surface text-ink-muted"
                  }`}
                >
                  Pulso activo HIGH
                </button>
                <button
                  type="button"
                  onClick={() => setPulsoAlto(false)}
                  className={`flex-1 rounded-lg border px-3 py-2 text-xs font-semibold transition ${
                    !pulsoAlto
                      ? "border-brand bg-brand-soft text-ink"
                      : "border-line-soft bg-surface text-ink-muted"
                  }`}
                >
                  Pulso activo LOW
                </button>
              </div>

              <div>
                <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
                  Retardo de apagado del relé tras fin de ciclo (ms)
                </label>
                <input
                  name="cafe_off_delay_ms"
                  type="number"
                  min="0"
                  step="1"
                  defaultValue={device.cafe_off_delay_ms}
                  className="w-full rounded-lg border border-line bg-surface px-3.5 py-2.5 text-sm text-ink outline-none focus:border-brand"
                />
              </div>

              <div className="grid grid-cols-2 gap-3">
                <div>
                  <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
                    Calentador activo (min)
                  </label>
                  <input
                    name="cafe_heat_on_min"
                    type="number"
                    min="1"
                    step="1"
                    defaultValue={device.cafe_heat_on_min}
                    className="w-full rounded-lg border border-line bg-surface px-3.5 py-2.5 text-sm text-ink outline-none focus:border-brand"
                  />
                </div>
                <div>
                  <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
                    Calentador apagado (min)
                  </label>
                  <input
                    name="cafe_heat_off_min"
                    type="number"
                    min="1"
                    step="1"
                    defaultValue={device.cafe_heat_off_min}
                    className="w-full rounded-lg border border-line bg-surface px-3.5 py-2.5 text-sm text-ink outline-none focus:border-brand"
                  />
                </div>
              </div>

              <div>
                <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
                  Comando de reset remoto por BLE
                </label>
                <input
                  name="cafe_bt_reset_cmd"
                  maxLength={40}
                  defaultValue={device.cafe_bt_reset_cmd}
                  className="w-full rounded-lg border border-line bg-surface px-3.5 py-2.5 text-sm text-ink outline-none focus:border-brand"
                />
              </div>
            </div>
          </details>

          {state?.error && (
            <p className="text-xs font-medium text-danger">{state.error}</p>
          )}
          {state?.ok && (
            <p className="text-xs font-medium text-ok">
              Guardado ✓ — se aplica en el próximo check-in de la placa.
            </p>
          )}

          <button
            type="submit"
            disabled={pending}
            className="w-full rounded-lg bg-brand py-3 text-sm font-semibold text-white transition hover:opacity-90 disabled:opacity-60"
          >
            {pending ? "Guardando…" : "Guardar cambios"}
          </button>
        </form>

        <div className="mt-4 flex items-center gap-2.5 border-t border-line-soft pt-4">
          <button
            type="button"
            disabled={limpiezaPendiente}
            onClick={dispararLimpieza}
            className="rounded-[9px] border border-line-soft bg-surface-2 px-4 py-2.5 text-[13px] font-semibold text-ink disabled:opacity-50"
          >
            🧽 Limpiar ahora
          </button>
          {limpiezaOk && (
            <span className="text-[11px] text-ok">
              ✓ Enviado — se ejecuta en el próximo check-in de la placa
            </span>
          )}
        </div>
      </div>

      <div className="rounded-lg border border-info-line bg-info-bg px-4 py-4 text-xs text-info">
        💡 La red WiFi de la máquina se configura directo desde la placa
        (red &quot;QRcade_Config&quot;). Todo lo demás es desde acá.
      </div>
    </div>
  );
}
