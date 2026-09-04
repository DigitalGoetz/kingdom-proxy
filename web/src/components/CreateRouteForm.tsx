import { useState, type FormEvent } from "react";
import type { NewRouteInput } from "../types";
import { containerNameError, containerPortError, pathPrefixError } from "../validation";

export function CreateRouteForm({
  reservedPathPrefix,
  onCreate,
}: {
  reservedPathPrefix: string;
  onCreate: (input: NewRouteInput) => Promise<void>;
}) {
  const [pathPrefix, setPathPrefix] = useState("");
  const [containerName, setContainerName] = useState("");
  const [containerPort, setContainerPort] = useState("");
  const [stripPrefix, setStripPrefix] = useState(true);
  const [submitting, setSubmitting] = useState(false);
  const [fieldError, setFieldError] = useState<string | null>(null);

  const reset = () => {
    setPathPrefix("");
    setContainerName("");
    setContainerPort("");
    setStripPrefix(true);
  };

  const handleSubmit = async (e: FormEvent) => {
    e.preventDefault();
    setFieldError(null);

    const port = Number(containerPort);
    const error =
      pathPrefixError(pathPrefix, reservedPathPrefix) ??
      containerNameError(containerName) ??
      containerPortError(port);
    if (error) {
      setFieldError(error);
      return;
    }

    setSubmitting(true);
    try {
      await onCreate({ path_prefix: pathPrefix, container_name: containerName, container_port: port, strip_prefix: stripPrefix });
      reset();
    } catch (e) {
      setFieldError(e instanceof Error ? e.message : String(e));
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <section className="panel">
      <h2>Create route</h2>
      <form className="route-form" onSubmit={handleSubmit}>
        <div className="field">
          <label htmlFor="path_prefix">Path prefix</label>
          <input
            id="path_prefix"
            type="text"
            placeholder="/myapp"
            value={pathPrefix}
            onChange={(e) => setPathPrefix(e.target.value)}
            required
          />
        </div>
        <div className="field">
          <label htmlFor="container_name">Container name</label>
          <input
            id="container_name"
            type="text"
            placeholder="my-container"
            value={containerName}
            onChange={(e) => setContainerName(e.target.value)}
            required
          />
        </div>
        <div className="field">
          <label htmlFor="container_port">Port</label>
          <input
            id="container_port"
            type="number"
            placeholder="8080"
            min={1}
            max={65535}
            value={containerPort}
            onChange={(e) => setContainerPort(e.target.value)}
            required
          />
        </div>
        <label className="checkbox-field">
          <input
            type="checkbox"
            checked={stripPrefix}
            onChange={(e) => setStripPrefix(e.target.checked)}
          />
          Strip prefix
        </label>
        <button type="submit" className="primary" disabled={submitting}>
          {submitting ? "Creating…" : "Create route"}
        </button>
      </form>
      {fieldError && <p className="error-banner" style={{ marginTop: 12, marginBottom: 0 }}>{fieldError}</p>}
    </section>
  );
}
