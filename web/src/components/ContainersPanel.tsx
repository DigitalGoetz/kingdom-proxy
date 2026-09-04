import type { ContainerSummary } from "../types";

export function ContainersPanel({
  containers,
  networkName,
  onSelectTarget,
}: {
  containers: ContainerSummary[];
  networkName: string;
  onSelectTarget: (containerName: string, port: number) => void;
}) {
  return (
    <section className="panel">
      <h2>Running containers</h2>
      <p className="subtitle" style={{ margin: "-4px 0 12px" }}>
        Read-only — every container on the host, not just this stack's. Click a port to fill it
        into the form above.
      </p>
      {containers.length === 0 ? (
        <p className="empty-state">No running containers found.</p>
      ) : (
        <table>
          <thead>
            <tr>
              <th>Name</th>
              <th>Image</th>
              <th>Status</th>
              <th>Ports</th>
            </tr>
          </thead>
          <tbody>
            {containers.map((container) => (
              <tr key={container.id}>
                <td><code>{container.name}</code></td>
                <td className="target-cell">{container.image}</td>
                <td style={{ color: "var(--text-muted)", fontSize: 12.5 }}>{container.status}</td>
                <td>
                  {container.ports.length === 0 ? (
                    <span style={{ color: "var(--text-muted)" }}>—</span>
                  ) : (
                    <span style={{ display: "flex", gap: 6, flexWrap: "wrap", alignItems: "center" }}>
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
                      {!container.on_network && (
                        <span
                          className="badge disabled"
                          title={`Not on the ${networkName} network yet -- a route will 502 until you run: docker network connect ${networkName} ${container.name}`}
                        >
                          not on {networkName}
                        </span>
                      )}
                    </span>
                  )}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </section>
  );
}
