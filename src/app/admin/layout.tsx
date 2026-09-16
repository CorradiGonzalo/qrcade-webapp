import { requireAdmin } from "@/lib/supabase/auth";
import { AdminSidebar } from "@/components/AdminSidebar";

export default async function AdminLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  const { profile } = await requireAdmin();

  return (
    <div className="flex min-h-screen bg-bg">
      <AdminSidebar displayName={profile.display_name ?? profile.username} />
      <div className="flex-1 overflow-hidden px-10 py-8">{children}</div>
    </div>
  );
}
