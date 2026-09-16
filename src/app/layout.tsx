import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "QRcade · Panel",
  description: "Panel de administración de máquinas QRcade",
};

export default function RootLayout({ children }: LayoutProps<"/">) {
  return (
    <html lang="es" className="h-full antialiased">
      <body className="min-h-full flex flex-col bg-bg text-ink">
        {children}
      </body>
    </html>
  );
}
