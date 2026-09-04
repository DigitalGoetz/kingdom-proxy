import { useCallback, useEffect, useRef, useState } from "react";
import { api } from "./api";
import { CreateRouteForm } from "./components/CreateRouteForm";
import { RoutesTable } from "./components/RoutesTable";
import { StatusBar } from "./components/StatusBar";
import type { Config, NewRouteInput, RouteUpdateInput } from "./types";

const POLL_INTERVAL_MS = 5000;

export default function App() {
  const [config, setConfig] = useState<Config | null>(null);
  const [loadError, setLoadError] = useState<string | null>(null);
  const pollRef = useRef<ReturnType<typeof setInterval>>();

  const refresh = useCallback(async () => {
    try {
      const next = await api.getConfig();
      setConfig(next);
      setLoadError(null);
    } catch (e) {
      setLoadError(e instanceof Error ? e.message : String(e));
    }
  }, []);

  useEffect(() => {
    refresh();
    pollRef.current = setInterval(refresh, POLL_INTERVAL_MS);
    return () => clearInterval(pollRef.current);
  }, [refresh]);

  const handleCreate = async (input: NewRouteInput) => {
    await api.createRoute(input);
    await refresh();
  };

  const handleUpdate = async (id: number, patch: RouteUpdateInput) => {
    await api.updateRoute(id, patch);
    await refresh();
  };

  const handleDelete = async (id: number) => {
    await api.deleteRoute(id);
    await refresh();
  };

  return (
    <>
      <h1>kingdom-proxy</h1>
      <p className="subtitle">Path-based routes for the nginx reverse proxy in front of this dashboard.</p>

      {loadError && <p className="error-banner">Couldn't reach the admin API: {loadError}</p>}

      {config && (
        <>
          <div className="panel">
            <StatusBar sync={config.sync} reservedPathPrefix={config.reserved_path_prefix} />
          </div>
          <CreateRouteForm reservedPathPrefix={config.reserved_path_prefix} onCreate={handleCreate} />
          <RoutesTable routes={config.routes} onUpdate={handleUpdate} onDelete={handleDelete} />
        </>
      )}
    </>
  );
}
