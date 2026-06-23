// Tiny fetch wrapper: GET when no body, POST JSON otherwise. Returns parsed JSON
// (or null for an empty body). Every route is generic/data-driven on the server,
// so the UI never hardcodes effect-specific knowledge.
export async function api(path, body) {
  const opt = body
    ? { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }
    : {};
  const res = await fetch(path, opt);
  if (!res.ok) throw new Error(`${path} -> ${res.status}`);
  const text = await res.text();
  return text ? JSON.parse(text) : null;
}
