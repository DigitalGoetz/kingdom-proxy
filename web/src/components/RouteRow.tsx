import { useState } from "react";
import type { Route, RouteUpdateInput } from "../types";
import { containerNameError, containerPortError } from "../validation";

export function RouteRow({
  route,
  onUpdate,
  onDelete,
}: {
  route: Route;
  onUpdate: (id: number, patch: RouteUpdateInput) => Promise<void>;
  onDelete: (id: number) => Promise<void>;
}) {
  const [editing, setEditing] = useState(false);
  const [containerName, setContainerName] = useState(route.container_name);
  const [containerPort, setContainerPort] = useState(String(route.container_port));
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const startEdit = () => {
    setContainerName(route.container_name);
    setContainerPort(String(route.container_port));
    setError(null);
    setEditing(true);
  };

  const save = async () => {
    const port = Number(containerPort);
    const validationError = containerNameError(containerName) ?? containerPortError(port);
    if (validationError) {
      setError(validationError);
      return;
    }
    setBusy(true);
    setError(null);
    try {
      await onUpdate(route.id, { container_name: containerName, container_port: port });
      setEditing(false);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };

  const toggleEnabled = async () => {
    setBusy(true);
    setError(null);
    try {
      await onUpdate(route.id, { enabled: !route.enabled });
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };

  const remove = async () => {
    if (!confirm(`Delete route ${route.path_prefix}?`)) return;
    setBusy(true);
    setError(null);
    try {
      await onDelete(route.id);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };

  if (editing) {
    return (
      <tr>
        <td><code>{route.path_prefix}</code></td>
        <td className="target-cell">
          <input type="text" value={containerName} onChange={(e) => setContainerName(e.target.value)} style={{ width: "45%", marginRight: 4 }} />
          :
          <input type="number" value={containerPort} onChange={(e) => setContainerPort(e.target.value)} style={{ width: "35%", marginLeft: 4 }} />
        </td>
        <td colSpan={3}>
          {error && <span style={{ color: "var(--danger)", fontSize: 12 }}>{error}</span>}
        </td>
        <td className="actions">
          <button className="small primary" disabled={busy} onClick={save}>Save</button>
          <button className="small" disabled={busy} onClick={() => setEditing(false)}>Cancel</button>
        </td>
      </tr>
    );
  }

  return (
    <>
      <tr>
        <td><code>{route.path_prefix}</code></td>
        <td className="target-cell">
          {route.container_name}:{route.container_port}
          {route.strip_prefix ? "" : " (full path)"}
        </td>
        <td>
          <span className={`badge ${route.enabled ? "enabled" : "disabled"}`}>
            {route.enabled ? "enabled" : "disabled"}
          </span>
        </td>
        <td style={{ color: "var(--text-muted)", fontSize: 12.5 }}>{route.last_seen_ip || "—"}</td>
        <td style={{ color: "var(--text-muted)", fontSize: 12.5 }}>{formatRelative(route.updated_at)}</td>
        <td className="actions">
          <a
            className="btn small"
            href={`${route.path_prefix}/`}
            target="_blank"
            rel="noopener noreferrer"
            title={`Open ${route.path_prefix}/ in a new tab`}
          >
            Open ↗
          </a>
          <button className="small" disabled={busy} onClick={toggleEnabled}>
            {route.enabled ? "Disable" : "Enable"}
          </button>
          <button className="small" disabled={busy} onClick={startEdit}>Edit</button>
          <button className="small danger" disabled={busy} onClick={remove}>Delete</button>
        </td>
      </tr>
      {error && (
        <tr>
          <td colSpan={6} style={{ color: "var(--danger)", fontSize: 12, paddingTop: 0 }}>{error}</td>
        </tr>
      )}
    </>
  );
}

function formatRelative(isoTimestamp: string): string {
  if (!isoTimestamp) return "—";
  const date = new Date(isoTimestamp);
  if (Number.isNaN(date.getTime())) return isoTimestamp;

  const seconds = Math.round((date.getTime() - Date.now()) / 1000);
  const divisions: [Intl.RelativeTimeFormatUnit, number][] = [
    ["second", 60],
    ["minute", 60],
    ["hour", 24],
    ["day", 30],
    ["month", 12],
    ["year", Infinity],
  ];
  const rtf = new Intl.RelativeTimeFormat("en", { numeric: "auto" });

  let value = seconds;
  let unit: Intl.RelativeTimeFormatUnit = "second";
  for (const [nextUnit, amount] of divisions) {
    unit = nextUnit;
    if (Math.abs(value) < amount) break;
    value = Math.trunc(value / amount);
  }
  return rtf.format(value, unit);
}
