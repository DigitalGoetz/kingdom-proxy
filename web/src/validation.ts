// Mirrors the validation in src/api/server.cpp (is_valid_path_prefix /
// is_valid_container_name / is_valid_port) so the form can give immediate
// feedback -- the server re-validates everything regardless, and its error
// message is always what's shown on rejection.

const PATH_PREFIX_RE = /^\/[A-Za-z0-9._-]+(\/[A-Za-z0-9._-]+)*$/;
const CONTAINER_NAME_RE = /^[A-Za-z0-9][A-Za-z0-9_.-]{0,254}$/;

export function pathPrefixError(value: string, reservedPrefix: string): string | null {
  if (!PATH_PREFIX_RE.test(value)) {
    return "Must start with '/' and contain only letters, digits, '.', '_', '-' and '/' (no trailing or repeated slashes).";
  }
  if (value === reservedPrefix || value.startsWith(`${reservedPrefix}/`)) {
    return `'${reservedPrefix}' is reserved for the admin API itself.`;
  }
  return null;
}

export function containerNameError(value: string): string | null {
  return CONTAINER_NAME_RE.test(value) ? null : "Not a valid docker container name.";
}

export function containerPortError(value: number): string | null {
  return Number.isInteger(value) && value > 0 && value <= 65535
    ? null
    : "Must be an integer between 1 and 65535.";
}
