import { useCallback, useEffect, useRef, useState } from "react";
import { api } from "./api";
import { ContainersPanel } from "./components/ContainersPanel";
import { CreateRouteForm, type TargetPrefill } from "./components/CreateRouteForm";
import { RoutesTable } from "./components/RoutesTable";
import { StatusBar } from "./components/StatusBar";
import type { Config, ContainerSummary, NewRouteInput, RouteUpdateInput } from "./types";

const POLL_INTERVAL_MS = 5000;

export default function App() {
  const [config, setConfig] = useState<Config | null>(null);
  const [loadError, setLoadError] = useState<string | null>(null);
  const [containers, setContainers] = useState<ContainerSummary[]>([]);
  const [containersError, setContainersError] = useState<string | null>(null);
  const [prefill, setPrefill] = useState<TargetPrefill | null>(null);
  const pollRef = useRef<ReturnType<typeof setInterval>>();

  const refresh = useCallback(async () => {
    try {
      const next = await api.getConfig();
      setConfig(next);
      setLoadError(null);
    } catch (e) {
      setLoadError(e instanceof Error ? e.message : String(e));
    }

    // Kept independent of the config fetch above: docker being briefly
    // unreachable shouldn't take down the route dashboard, just this panel.
    try {
      setContainers(await api.getContainers());
      setContainersError(null);
    } catch (e) {
      setContainersError(e instanceof Error ? e.message : String(e));
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

  const handleAttach = async (containerName: string) => {
    await api.attachContainer(containerName);
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
          <CreateRouteForm
            reservedPathPrefix={config.reserved_path_prefix}
            prefill={prefill}
            onCreate={handleCreate}
          />
          <RoutesTable routes={config.routes} onUpdate={handleUpdate} onDelete={handleDelete} />
          {containersError ? (
            <p className="error-banner">Couldn't list containers: {containersError}</p>
          ) : (
            <ContainersPanel
              containers={containers}
              networkName={config.network_name}
              onSelectTarget={(containerName, containerPort) =>
                setPrefill({ containerName, containerPort })
              }
              onAttach={handleAttach}
            />
          )}
        </>
      )}
    </>
  );
}
