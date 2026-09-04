import type { ContainerSummary } from "../types";
import { ContainerRow } from "./ContainerRow";

export function ContainersPanel({
  containers,
  networkName,
  onSelectTarget,
  onAttach,
}: {
  containers: ContainerSummary[];
  networkName: string;
  onSelectTarget: (containerName: string, port: number) => void;
  onAttach: (containerName: string) => Promise<void>;
}) {
  return (
    <section className="panel">
      <h2>Running containers</h2>
      <p className="subtitle" style={{ margin: "-4px 0 12px" }}>
        Read-only, except for Attach — every container on the host, not just this stack's. Click a
        port to fill it into the form above; a container not on {networkName} needs Attach before a
        route to it will work.
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
              <th>Network</th>
            </tr>
          </thead>
          <tbody>
            {containers.map((container) => (
              <ContainerRow
                key={container.id}
                container={container}
                networkName={networkName}
                onSelectTarget={onSelectTarget}
                onAttach={onAttach}
              />
            ))}
          </tbody>
        </table>
      )}
    </section>
  );
}
