/**
 * Resuelve el filtro de período para la sección de Ventas (ver
 * SalesPeriodFilter + dashboard/maquinas/[id]/page.tsx) a partir de los
 * query params de la URL. Todo el estado del filtro vive en la URL, acá
 * sólo se traduce a un rango de fechas concreto para la query a Supabase.
 *
 * OJO simplificación: las fechas "Hoy" / "Este mes" se calculan con la
 * hora del server (no la del dueño) — para una app de un solo país/huso
 * horario como esta no vale la pena la complejidad extra de manejar
 * timezone por usuario.
 */

export type PeriodoKey = "hoy" | "7d" | "30d" | "mes" | "todo" | "custom";

export interface RangoFechas {
  periodo: PeriodoKey;
  /** Inclusive. null = sin límite inferior ("todo"). */
  desde: Date | null;
  /** Exclusivo (el día siguiente a las 00:00). null = sin límite superior. */
  hasta: Date | null;
  /** Para mostrar en la UI. */
  label: string;
}

function inicioDeHoy(): Date {
  const d = new Date();
  d.setHours(0, 0, 0, 0);
  return d;
}

function sumarDias(d: Date, dias: number): Date {
  const r = new Date(d);
  r.setDate(r.getDate() + dias);
  return r;
}

export function resolverRangoFechas(params: {
  periodo?: string;
  desde?: string;
  hasta?: string;
}): RangoFechas {
  const periodo = (params.periodo as PeriodoKey) || "30d";
  const hoy = inicioDeHoy();

  switch (periodo) {
    case "hoy":
      return { periodo, desde: hoy, hasta: sumarDias(hoy, 1), label: "Hoy" };

    case "7d":
      return {
        periodo,
        desde: sumarDias(hoy, -6),
        hasta: sumarDias(hoy, 1),
        label: "Últimos 7 días",
      };

    case "mes": {
      const inicioMes = new Date(hoy.getFullYear(), hoy.getMonth(), 1);
      return {
        periodo,
        desde: inicioMes,
        hasta: sumarDias(hoy, 1),
        label: "Este mes",
      };
    }

    case "todo":
      return { periodo, desde: null, hasta: null, label: "Todo el historial" };

    case "custom": {
      const desde = params.desde ? new Date(params.desde + "T00:00:00") : null;
      const hastaBase = params.hasta ? new Date(params.hasta + "T00:00:00") : null;
      if (!desde || !hastaBase || Number.isNaN(desde.getTime()) || Number.isNaN(hastaBase.getTime())) {
        // Rango inválido/incompleto — fallback a 30 días en vez de romper la página.
        return {
          periodo: "30d",
          desde: sumarDias(hoy, -29),
          hasta: sumarDias(hoy, 1),
          label: "Últimos 30 días",
        };
      }
      return {
        periodo,
        desde,
        hasta: sumarDias(hastaBase, 1),
        label: `${params.desde} al ${params.hasta}`,
      };
    }

    case "30d":
    default:
      return {
        periodo: "30d",
        desde: sumarDias(hoy, -29),
        hasta: sumarDias(hoy, 1),
        label: "Últimos 30 días",
      };
  }
}
