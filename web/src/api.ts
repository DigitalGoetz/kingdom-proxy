import type { Config, ContainerSummary, NewRouteInput, RouteUpdateInput, RouteWithWarning } from "./types";

// Reaches kingdom-proxy-api through the proxy itself, on the same origin
// this page is served from -- see nginx.baseline.conf's "/_proxy/" location.
// (During `npm run dev`, vite.config.ts proxies this same path to a running
// docker-compose stack.)
const API_BASE = "/_proxy";

export class ApiRequestError extends Error {
  constructor(
    message: string,
    public status: number,
  ) {
    super(message);
    this.name = "ApiRequestError";
  }
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(`${API_BASE}${path}`, {
    ...init,
    headers: { "content-type": "application/json", ...init?.headers },
  });

  if (!res.ok) {
    let message = `request failed with status ${res.status}`;
    try {
      const body = await res.json();
      if (typeof body?.error === "string") message = body.error;
    } catch {
      // Response body wasn't JSON (or was empty) -- fall back to the
      // generic message above.
    }
    throw new ApiRequestError(message, res.status);
  }

  if (res.status === 204) return undefined as T;
  return (await res.json()) as T;
}

export const api = {
  getConfig: () => request<Config>("/config"),
  getContainers: () => request<ContainerSummary[]>("/containers"),

  createRoute: (input: NewRouteInput) =>
    request<RouteWithWarning>("/routes", { method: "POST", body: JSON.stringify(input) }),

  updateRoute: (id: number, patch: RouteUpdateInput) =>
    request<RouteWithWarning>(`/routes/${id}`, { method: "PATCH", body: JSON.stringify(patch) }),

  deleteRoute: (id: number) => request<void>(`/routes/${id}`, { method: "DELETE" }),
};
