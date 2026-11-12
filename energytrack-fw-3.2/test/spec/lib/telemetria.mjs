// O teste em C da telemetria e o JSON que ele imprime, para os testes em
// Node (contrato, backend de teste e bancada).
import assert from 'node:assert/strict';
import { runHostTest } from './host.mjs';

export const telemetry = () => runHostTest('test_telemetry', [
  'src/proc/telemetry.c', 'src/proc/anomaly.c', 'src/proc/goal.c',
]);

/** JSON impresso pelo teste em C como "# json-<caso>: {...}": { text, json }. */
export function printedJson(result, name) {
  const prefix = `# json-${name}: `;
  const line = result.output.split(/\r?\n/).find((l) => l.startsWith(prefix));
  assert.ok(line, `o teste em C não imprimiu o JSON "${name}"\n\nsaída:\n${result.output}`);
  const text = line.slice(prefix.length);
  try {
    return { text, json: JSON.parse(text) };
  } catch (err) {
    assert.fail(`"${name}" não é JSON válido (${err.message}):\n${text}`);
  }
}
