export interface Route {
  id: number;
  path_prefix: string;
  container_name: string;
  container_port: number;
  strip_prefix: boolean;
  enabled: boolean;
  last_seen_ip: string;
  created_at: string;
  updated_at: string;
}

export interface SyncStatus {
  last_sync_ok: boolean;
  last_error: string;
  last_sync_at: string;
  sync_count: number;
}

export interface Config {
  routes: Route[];
  reserved_path_prefix: string;
  network_name: string;
  sync: SyncStatus;
}

export interface NewRouteInput {
  path_prefix: string;
  container_name: string;
  container_port: number;
  // Not sent -- the server defaults new routes to stripping the prefix,
  // which is all this app's users need.
}

export interface RouteUpdateInput {
  container_name?: string;
  container_port?: number;
  strip_prefix?: boolean;
  enabled?: boolean;
}

export interface RouteWithWarning extends Route {
  warning?: string;
}

export interface ContainerSummary {
  id: string;
  name: string;
  image: string;
  status: string;
  ports: number[];
  on_network: boolean;
}

export interface ApiError {
  error: string;
}
