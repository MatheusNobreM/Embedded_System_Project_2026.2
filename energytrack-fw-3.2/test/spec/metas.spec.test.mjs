// Meta diária e limite de potência (US-004).
import { test } from 'node:test';
import { runHostTest, assertCriterion, assertAllPassed } from './lib/host.mjs';

const goal = () => runHostTest('test_goal', ['src/proc/goal.c']);

test('@spec:AC-008 — aviso de atenção em 80% e de meta estourada em 100%', () => {
  assertCriterion(goal(), 'AC-008');
});

test('@spec:AC-009 — projeção acima da meta avisa cedo', () => {
  assertCriterion(goal(), 'AC-009');
});

test('@spec:AC-010 — limite de potência sustentada', () => {
  assertCriterion(goal(), 'AC-010');
});

test('metas: todos os casos', () => {
  assertAllPassed(goal());
});
