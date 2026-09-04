import type { Route, RouteUpdateInput } from "../types";
import { RouteRow } from "./RouteRow";

export function RoutesTable({
  routes,
  onUpdate,
  onDelete,
}: {
  routes: Route[];
  onUpdate: (id: number, patch: RouteUpdateInput) => Promise<void>;
  onDelete: (id: number) => Promise<void>;
}) {
  return (
    <section className="panel">
      <h2>Routes ({routes.length})</h2>
      {routes.length === 0 ? (
        <p className="empty-state">No routes yet. Create one above.</p>
      ) : (
        <table>
          <thead>
            <tr>
              <th>Path</th>
              <th>Target</th>
              <th>Status</th>
              <th>Last seen IP</th>
              <th>Updated</th>
              <th></th>
            </tr>
          </thead>
          <tbody>
            {routes.map((route) => (
              <RouteRow key={route.id} route={route} onUpdate={onUpdate} onDelete={onDelete} />
            ))}
          </tbody>
        </table>
      )}
    </section>
  );
}
