import { redirect } from "next/navigation";
import { requireActiveUser } from "@/lib/supabase/auth";
import { UserSidebar } from "@/components/UserSidebar";

export default async function DashboardLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  const { profile } = await requireActiveUser();

  if (profile.role === "admin") redirect("/admin/equipos");

  return (
    <div className="flex min-h-screen bg-bg">
      <UserSidebar displayName={profile.display_name ?? profile.username} />
      <div className="flex-1 overflow-hidden px-10 py-8">{children}</div>
    </div>
  );
}
