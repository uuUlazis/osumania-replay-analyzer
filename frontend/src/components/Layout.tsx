import type { ReactNode } from "react";

export default function Layout({
  title,
  children,
}: {
  title: string;
  children: ReactNode;
}) {
  return (
    <div className="app" data-title={title}>
      {children}
    </div>
  );
}
