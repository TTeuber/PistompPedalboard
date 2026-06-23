// Tiny fetch wrapper: GET when no body, POST JSON otherwise. The caller names the
// expected response type (defaults to `unknown` so it can't be used unchecked).
// Every route is generic/data-driven on the server, so the UI never hardcodes
// effect-specific knowledge.
export async function api<T = unknown>(path: string, body?: unknown): Promise<T> {
  const opt: RequestInit = body
    ? { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }
    : {};
  const res = await fetch(path, opt);
  if (!res.ok) throw new Error(`${path} -> ${res.status}`);
  const text = await res.text();
  return (text ? JSON.parse(text) : null) as T;
}
