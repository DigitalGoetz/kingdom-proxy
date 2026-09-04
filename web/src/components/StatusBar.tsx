import type { SyncStatus } from "../types";

export function StatusBar({ sync, reservedPathPrefix }: { sync: SyncStatus; reservedPathPrefix: string }) {
  return (
    <div className="status-bar">
      <span>
        <span className={`status-dot ${sync.last_sync_ok ? "ok" : "error"}`} />
        {sync.last_sync_ok ? "nginx in sync" : `sync failed: ${sync.last_error || "unknown error"}`}
      </span>
      <span>·</span>
      <span>{sync.sync_count} sync{sync.sync_count === 1 ? "" : "s"} so far</span>
      <span>·</span>
      <span><code>{reservedPathPrefix}</code> is reserved for this admin API</span>
    </div>
  );
}
