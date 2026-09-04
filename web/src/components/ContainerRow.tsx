import { useState } from "react";
import type { ContainerSummary } from "../types";

export function ContainerRow({
  container,
  networkName,
  onSelectTarget,
  onAttach,
}: {
  container: ContainerSummary;
  networkName: string;
  onSelectTarget: (containerName: string, port: number) => void;
  onAttach: (containerName: string) => Promise<void>;
}) {
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const attach = async () => {
    setBusy(true);
    setError(null);
    try {
      await onAttach(container.name);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };

  return (
    <>
      <tr>
        <td><code>{container.name}</code></td>
        <td className="target-cell">{container.image}</td>
        <td style={{ color: "var(--text-muted)", fontSize: 12.5 }}>{container.status}</td>
        <td>
          {container.ports.length === 0 ? (
            <span style={{ color: "var(--text-muted)" }}>—</span>
          ) : (
            <span style={{ display: "flex", gap: 6, flexWrap: "wrap" }}>
              {container.ports.map((port) => (
                <button
                  key={port}
                  type="button"
                  className="small"
                  title={`Use ${container.name}:${port} in the create-route form above`}
                  onClick={() => onSelectTarget(container.name, port)}
                >
                  {port}
                </button>
              ))}
            </span>
          )}
        </td>
        <td>
          {container.on_network ? (
            <span className="badge enabled">on {networkName}</span>
          ) : (
            <span style={{ display: "flex", gap: 6, alignItems: "center", flexWrap: "wrap" }}>
              <span className="badge disabled">not on {networkName}</span>
              <button
                type="button"
                className="small primary"
                disabled={busy}
                title={`Run the equivalent of: docker network connect ${networkName} ${container.name}`}
                onClick={attach}
              >
                {busy ? "Attaching…" : "Attach"}
              </button>
            </span>
          )}
        </td>
      </tr>
      {error && (
        <tr>
          <td colSpan={5} style={{ color: "var(--danger)", fontSize: 12, paddingTop: 0 }}>{error}</td>
        </tr>
      )}
    </>
  );
}
