// Tempo real (US-006): monitor de deadline e análise de tempo de resposta.
import { test } from 'node:test';
import { runHostTest, assertCriterion, assertAllPassed } from './lib/host.mjs';

const rtSources = ['src/rt/rt_monitor.c', 'src/rt/rt_sched.c', 'src/rt/rt_tasks.c'];
const rt = () => runHostTest('test_rt', rtSources);

test('@spec:AC-016 — monitor acusa estouro de deadline', () => {
  assertCriterion(rt(), 'AC-016');
});

test('@spec:AC-017 — análise de tempo de resposta prova que as tarefas cabem', () => {
  assertCriterion(rt(), 'AC-017');
});

test('tempo real: formato do log e regras da tabela de tarefas', () => {
  assertAllPassed(rt());
});
