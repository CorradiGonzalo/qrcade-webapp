import { NextResponse } from "next/server";
import QRCode from "qrcode";
import { PDFDocument, StandardFonts, rgb } from "pdf-lib";
import { createClient } from "@/lib/supabase/server";
import type { Device } from "@/lib/supabase/types";

/**
 * Descarga en PDF el QR de cobro de una máquina. Pensado sobre todo para
 * las cafeteras (sin pantalla): la garra dibuja el QR en su propia
 * pantallita, pero una cafetera necesita que el dueño lo imprima y lo
 * pegue físicamente en la máquina — sin esto no hay forma de cobrar.
 *
 * Mismo QR que ya se ve en el panel (device.qr_data, el EMV crudo que
 * devuelve Mercado Pago al armar la caja) — esto sólo lo pone en un PDF
 * listo para imprimir, no genera nada nuevo en Mercado Pago.
 */
export async function GET(
  _request: Request,
  { params }: { params: Promise<{ id: string }> }
) {
  const { id } = await params;
  const supabase = await createClient();

  const {
    data: { user },
  } = await supabase.auth.getUser();

  if (!user) {
    return NextResponse.json({ error: "No autenticado." }, { status: 401 });
  }

  const { data: device } = await supabase
    .from("devices")
    .select("*")
    .eq("id", id)
    .eq("owner_id", user.id)
    .single<Device>();

  if (!device) {
    return NextResponse.json({ error: "No se encontró la máquina." }, { status: 404 });
  }

  if (!device.qr_data) {
    return NextResponse.json(
      { error: "Esta máquina todavía no tiene el cobro configurado." },
      { status: 400 }
    );
  }

  const qrPngBytes = await QRCode.toBuffer(device.qr_data, {
    type: "png",
    margin: 1,
    width: 700,
  });

  const pdfDoc = await PDFDocument.create();
  const page = pdfDoc.addPage([420, 580]); // tamaño chico, cómodo para imprimir y pegar en la máquina
  const { width } = page.getSize();

  const fontBold = await pdfDoc.embedFont(StandardFonts.HelveticaBold);
  const fontRegular = await pdfDoc.embedFont(StandardFonts.Helvetica);

  const titulo = device.alias ?? device.mac;
  const tituloAncho = fontBold.widthOfTextAtSize(titulo, 20);
  page.drawText(titulo, {
    x: (width - tituloAncho) / 2,
    y: 520,
    size: 20,
    font: fontBold,
    color: rgb(0.1, 0.1, 0.1),
  });

  const subtitulo =
    device.mode === "fijo"
      ? `$${device.mp_monto_fijo ?? "—"} por crédito`
      : "Monto abierto — ver precios en la máquina";
  const subtituloAncho = fontRegular.widthOfTextAtSize(subtitulo, 12);
  page.drawText(subtitulo, {
    x: (width - subtituloAncho) / 2,
    y: 495,
    size: 12,
    font: fontRegular,
    color: rgb(0.35, 0.35, 0.35),
  });

  const qrImage = await pdfDoc.embedPng(qrPngBytes);
  const qrSize = 320;
  page.drawImage(qrImage, {
    x: (width - qrSize) / 2,
    y: 140,
    width: qrSize,
    height: qrSize,
  });

  const leyenda = "Escaneá con la app de Mercado Pago para pagar";
  const leyendaAncho = fontRegular.widthOfTextAtSize(leyenda, 11);
  page.drawText(leyenda, {
    x: (width - leyendaAncho) / 2,
    y: 105,
    size: 11,
    font: fontRegular,
    color: rgb(0.35, 0.35, 0.35),
  });

  page.drawText("QRcade", {
    x: (width - fontBold.widthOfTextAtSize("QRcade", 10)) / 2,
    y: 40,
    size: 10,
    font: fontBold,
    color: rgb(0.6, 0.6, 0.6),
  });

  const pdfBytes = await pdfDoc.save();

  const nombreBase = (device.alias ?? device.mac)
    .toString()
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/(^-|-$)/g, "");

  return new NextResponse(Buffer.from(pdfBytes), {
    headers: {
      "Content-Type": "application/pdf",
      "Content-Disposition": `attachment; filename="qr-${nombreBase || device.mac}.pdf"`,
      "Cache-Control": "no-store",
    },
  });
}
