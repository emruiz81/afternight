#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for product_measurement.py: the schedule, the paired decision rule, the comparison's
refusals, the archive's selection and verification, the application path, the pinned configuration and the recorded
baselines."""

from __future__ import annotations

import copy
import hashlib
import json
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest
import unittest.mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import product_application_driver_linux as linux_driver  # noqa: E402
import product_measurement as measurement  # noqa: E402


def git(root: pathlib.Path, *arguments: str) -> str:
    return subprocess.run(['git', '-C', str(root), *arguments], capture_output=True, text=True, check=True).stdout.strip()


class ScheduleTest(unittest.TestCase):
    def test_every_pair_holds_both_sides_in_a_seeded_order(self) -> None:
        schedule = measurement.build_schedule(12, 7)
        self.assertEqual(len(schedule), 12)
        for pair in schedule:
            self.assertEqual(sorted(pair), sorted(measurement.SIDES))
        self.assertEqual(schedule, measurement.build_schedule(12, 7))
        self.assertNotEqual(schedule, measurement.build_schedule(12, 8))
        self.assertEqual(len({pair[0] for pair in schedule}), 2, 'both sides lead some pair')

    def test_an_incremental_side_alternates_from_its_prime(self) -> None:
        self.assertEqual([measurement.incremental_variant(index) for index in range(5)], [1, 0, 1, 0, 1])


class DecisionTest(unittest.TestCase):
    """The paired decision rule, at a no-regression ratio of 1.0."""

    def decide(self, baseline, candidate):
        return measurement.decide(baseline, candidate, 1.0, 0.05, 0.05)

    def test_the_binomial_tail_is_exact(self) -> None:
        self.assertAlmostEqual(measurement.binomial_upper_tail(12, 12), 1 / 4096)
        self.assertAlmostEqual(measurement.binomial_upper_tail(12, 10), 79 / 4096)
        self.assertAlmostEqual(measurement.binomial_upper_tail(12, 9), 299 / 4096)
        self.assertAlmostEqual(measurement.binomial_upper_tail(4, 0), 1.0)

    def test_the_dispersion_is_the_sample_deviation_over_the_mean(self) -> None:
        self.assertAlmostEqual(measurement.coefficient_of_variation([9.0, 11.0]), 2 ** 0.5 / 10.0)
        self.assertEqual(measurement.coefficient_of_variation([5.0]), 0.0)

    def test_a_quiet_series_is_decided_by_its_medians(self) -> None:
        within = self.decide([100.0] * 12, [99.0] * 12)
        self.assertEqual((within['decision'], within['noisy']), ('within', False))
        regression = self.decide([100.0] * 12, [101.0] * 12)
        self.assertEqual(regression['decision'], 'regression')
        self.assertEqual(self.decide([100.0] * 12, [100.0] * 12)['decision'], 'within')

    def test_a_noisy_series_needs_the_sign_test_to_pass(self) -> None:
        baseline = [100.0, 130.0] * 6
        faster = [value * 0.95 for value in baseline]
        self.assertTrue(self.decide(baseline, faster)['noisy'])
        self.assertEqual(self.decide(baseline, faster)['decision'], 'within')
        mixed = [value * (0.9 if index < 9 else 1.1) for index, value in enumerate(baseline)]
        self.assertEqual(self.decide(baseline, mixed)['decision'], 'inconclusive')
        slower = [value * (1.1 if index < 11 else 0.9) for index, value in enumerate(baseline)]
        self.assertEqual(self.decide(baseline, slower)['decision'], 'regression')

    def test_one_noisy_side_is_enough_for_the_sign_test(self) -> None:
        # The candidate's median is under the baseline's, which a quiet series would accept; seven pairs of twelve
        # within the floor is not significant, so the noisy series is inconclusive.
        decision = self.decide([100.0] * 12, [90.0] * 7 + [120.0] * 5)
        self.assertTrue(decision['noisy'])
        self.assertEqual(decision['decision'], 'inconclusive')

    def test_ten_of_twelve_decides_and_nine_does_not(self) -> None:
        baseline = [100.0, 130.0] * 6
        ten = [value * (0.9 if index < 10 else 1.1) for index, value in enumerate(baseline)]
        nine = [value * (0.9 if index < 9 else 1.1) for index, value in enumerate(baseline)]
        self.assertEqual(self.decide(baseline, ten)['decision'], 'within')
        self.assertEqual(self.decide(baseline, nine)['decision'], 'inconclusive')

    def test_identical_noisy_pairs_decide_nothing_against_the_floor(self) -> None:
        baseline = [100.0, 130.0] * 6
        self.assertEqual(self.decide(baseline, list(baseline))['decision'], 'within')

    def test_no_pair_is_no_decision(self) -> None:
        self.assertEqual(self.decide([], [])['decision'], 'inconclusive')

    def test_a_quiet_series_is_no_regression_while_its_median_ratio_is_within_the_resolution(self) -> None:
        # Two identical products differ by their measurement's noise; the host's resolution is the ratio below which a
        # quiet difference is that noise rather than a regression.
        within = measurement.decide([100.0] * 12, [101.0] * 12, 1.0, 0.05, 0.05, resolution=1.02)
        self.assertEqual(within['decision'], 'within')
        self.assertEqual(within['resolution'], 1.02)
        tight = measurement.decide([100.0] * 12, [101.0] * 12, 1.0, 0.05, 0.05, resolution=1.005)
        self.assertEqual(tight['decision'], 'regression')
        self.assertEqual(self.decide([100.0] * 12, [101.0] * 12)['decision'], 'regression')

    def test_the_resolution_leaves_the_sign_test_at_the_no_regression_ratio(self) -> None:
        baseline = [100.0, 130.0] * 6
        slower = [value * (1.1 if index < 11 else 0.9) for index, value in enumerate(baseline)]
        decision = measurement.decide(baseline, slower, 1.0, 0.05, 0.05, resolution=1.5)
        self.assertTrue(decision['noisy'])
        self.assertEqual(decision['decision'], 'regression')

    def test_the_resolution_is_twice_the_largest_excursion_of_the_hosts_baseline(self) -> None:
        comparisons = [
            {'wallTime': {'medianRatio': 0.99}, 'residentPeak': {'medianRatio': 1.0001}},
            {'wallTime': {'medianRatio': 1.004}, 'residentPeak': {'medianRatio': 0.99995}},
            {'cancellationLatencyMs': {'medianRatio': 3.0}},
        ]
        resolution = measurement.baseline_resolution(comparisons, 2.0)
        self.assertAlmostEqual(resolution['wallTime'], 1.02, places=9)
        self.assertAlmostEqual(resolution['residentPeak'], 1.0002, places=9)
        self.assertEqual(measurement.baseline_resolution([], 2.0), {})


FRESH_STAGES = ['BiasStacking', 'DarkStacking', 'FlatStacking', 'LightsRegistration', 'LightsAlignment',
                'LightsNormalization', 'LightsStacking']


def run(index, side, pair, total, peak, *, success=True, cancelled=False, mode='full', outputs=None, latency=None,
        stages=None):
    return {'index': index, 'side': side, 'pair': pair, 'totalMs': total, 'residentPeakBytes': peak,
            'success': success, 'cancelled': cancelled, 'executionMode': mode,
            'stages': list(stages if stages is not None else FRESH_STAGES),
            'outputs': outputs if outputs is not None else {'light-master:stack.fit': 'aa'},
            'cancellationLatencyMs': latency, 'cpuSeconds': 1.0}


def runs_record(runs, case='warm'):
    return {'schema': 1, 'workflow': 'full', 'case': case, 'host': 'bench-windows',
            'datasetIdentity': 'f' * 64, 'runs': runs}


class CompareTest(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.configuration = measurement.read_configuration()

    def write(self, name, record):
        path = pathlib.Path(self.directory.name) / name
        path.mkdir()
        (path / 'runs.json').write_text(json.dumps(record), encoding='utf-8')
        return path

    def pairs(self, count=12, candidate_scale=1.0, **overrides):
        runs = [run(0, 'baseline', None, 999.0, 1)]
        for pair in range(count):
            runs.append(run(2 * pair + 1, 'baseline', pair, 100.0 + pair, 1000))
            runs.append(run(2 * pair + 2, 'candidate', pair, (100.0 + pair) * candidate_scale, 1000, **overrides))
        return runs

    def test_equal_sides_are_no_regression(self) -> None:
        report = measurement.compare([self.write('a', runs_record(self.pairs()))], self.configuration)
        self.assertEqual(report['pairs'], 12)
        self.assertEqual(report['verdict'], 'no regression')
        self.assertTrue(report['outputs']['held'])

    def test_a_slower_candidate_is_a_regression(self) -> None:
        report = measurement.compare([self.write('a', runs_record(self.pairs(candidate_scale=1.02)))],
                                     dict(self.configuration, baselines=[]))
        self.assertEqual(report['wallTime']['decision'], 'regression')
        self.assertEqual(report['verdict'], 'regression')

    def test_a_host_with_a_recorded_baseline_is_decided_by_its_resolution(self) -> None:
        configuration = dict(self.configuration, baselines=[
            {'host': 'bench-linux', 'resolution': {'wallTime': 1.001, 'residentPeak': 1.0}},
            {'host': 'bench-windows', 'resolution': {'wallTime': 1.03, 'residentPeak': 1.0005}}])
        report = measurement.compare([self.write('a', runs_record(self.pairs(candidate_scale=1.02)))], configuration)
        self.assertEqual(report['resolution']['wallTime'], 1.03)
        self.assertEqual(report['resolution']['residentPeak'], 1.0005)
        self.assertIn('bench-windows', report['resolution']['source'])
        self.assertEqual(report['wallTime']['decision'], 'within')
        self.assertEqual(report['verdict'], 'no regression')

    def test_a_host_with_no_recorded_baseline_is_decided_at_the_no_regression_ratio(self) -> None:
        configuration = dict(self.configuration, baselines=[
            {'host': 'bench-linux', 'resolution': {'wallTime': 1.03, 'residentPeak': 1.0005}}])
        report = measurement.compare([self.write('a', runs_record(self.pairs(candidate_scale=1.02)))], configuration)
        self.assertEqual(report['resolution']['wallTime'], 1.0)
        self.assertEqual(report['resolution']['residentPeak'], 1.0)
        self.assertIn('no recorded driver baseline', report['resolution']['source'])
        self.assertEqual(report['verdict'], 'regression')

    def test_different_outputs_refuse_the_timing(self) -> None:
        record = runs_record(self.pairs(outputs={'light-master:stack.fit': 'bb'}))
        report = measurement.compare([self.write('a', record)], self.configuration)
        self.assertTrue(report['verdict'].startswith('refused'))
        self.assertEqual(len(report['outputs']['pairsDiffering']), 12)

    def differing_sides(self, candidate_ulp=0, difference=None, roles=('light-master',)):
        """Pairs whose sides publish other bytes: every digest differs across the sides, each run agrees with its
        side's first run, and the first runs are described against each other."""
        runs = []
        for pair in range(12):
            for offset, side in ((1, 'baseline'), (2, 'candidate')):
                entry = run(2 * pair + offset, side, pair, 100.0 + pair, 1000,
                            outputs={f'{role}:{role}.fit': side[0] * 64 for role in roles})
                if pair == 0:
                    entry['outputReference'] = True
                else:
                    entry['outputAgreement'] = {
                        f'{role}:{role}.fit': {'sameShape': True, 'identical': candidate_ulp == 0 or side == 'baseline',
                                               'samples': 1000,
                                               'differingSamples': 0 if side == 'baseline' else min(candidate_ulp, 1),
                                               'maxUlp': 0 if side == 'baseline' else candidate_ulp,
                                               'maxAbsoluteDifference': 0.0, 'reference': 1}
                        for role in roles}
                runs.append(entry)
        record = runs_record(runs)
        statistics = {'sameShape': True, 'locationShiftInNoise': 0.01, 'noiseRatio': 1.01,
                      'medianAbsoluteDifferenceInNoise': 0.05, 'p99AbsoluteDifferenceInNoise': 0.5, 'correlation': 0.9999}
        statistics.update(difference or {})
        record['acrossSides'] = {'variant0': {f'{role}:{role}.fit': dict(statistics, candidate=f'{role}:{role}.fit')
                                              for role in roles}}
        return record

    def contract(self, **within):
        bounds = {'maxAbsoluteLocationShiftInNoise': 0.1, 'noiseRatio': [0.9, 1.1],
                  'maxMedianAbsoluteDifferenceInNoise': 0.2, 'maxP99AbsoluteDifferenceInNoise': 2.0,
                  'minCorrelation': 0.999}
        return dict(self.configuration, outputContract={
            'withinSide': {'light-master': {'maxUlp': 4, 'maxDifferingShare': 0.01}, **within},
            'acrossSides': {role: dict(bounds) for role in ('bias-master', 'dark-master', 'flat-master',
                                                             'light-master')}})

    def test_differing_sides_outputs_are_held_to_the_accepted_difference_contract(self) -> None:
        report = measurement.compare([self.write('a', self.differing_sides())], self.contract())
        self.assertEqual(report['outputs']['contract'], 'accepted difference')
        self.assertTrue(report['outputs']['held'], report['outputs']['violations'])
        self.assertEqual(report['verdict'], 'no regression')

    def test_a_light_master_within_its_bound_of_its_sides_first_run_holds(self) -> None:
        report = measurement.compare([self.write('a', self.differing_sides(candidate_ulp=3))], self.contract())
        self.assertTrue(report['outputs']['held'], report['outputs']['violations'])
        self.assertEqual(report['outputs']['withinSide']['candidate']['light-master']['maxUlp'], 3)

    def test_a_light_master_past_its_bound_refuses_the_timing(self) -> None:
        report = measurement.compare([self.write('a', self.differing_sides(candidate_ulp=5))], self.contract())
        self.assertFalse(report['outputs']['held'])
        self.assertTrue(report['verdict'].startswith('refused'))

    def test_a_light_master_differing_in_too_many_samples_refuses_the_timing(self) -> None:
        record = self.differing_sides(candidate_ulp=1)
        for entry in record['runs']:
            for agreement in (entry.get('outputAgreement') or {}).values():
                if entry['side'] == 'candidate':
                    agreement['differingSamples'] = 50
        report = measurement.compare([self.write('a', record)], self.contract())
        self.assertFalse(report['outputs']['held'])
        self.assertEqual(report['outputs']['withinSide']['candidate']['light-master']['maxDifferingShare'], 0.05)

    def test_a_role_with_no_bound_is_exact_within_its_side(self) -> None:
        record = self.differing_sides(candidate_ulp=1, roles=('bias-master',))
        report = measurement.compare([self.write('a', record)], self.contract())
        self.assertFalse(report['outputs']['held'])
        self.assertTrue(any('bias-master' in violation for violation in report['outputs']['violations']))

    def test_a_role_that_may_change_its_grid_is_held_to_its_distribution_alone(self) -> None:
        moved = {'sameShape': False, 'correlation': None, 'medianAbsoluteDifferenceInNoise': None,
                 'p99AbsoluteDifferenceInNoise': None}
        contract = self.contract()
        report = measurement.compare([self.write('a', self.differing_sides(difference=moved))], contract)
        self.assertFalse(report['outputs']['held'], 'a role on one grid must keep it')
        contract['outputContract']['acrossSides']['light-master']['grid'] = 'any'
        report = measurement.compare([self.write('b', self.differing_sides(difference=moved))], contract)
        self.assertTrue(report['outputs']['held'], report['outputs']['violations'])
        report = measurement.compare([self.write('c', self.differing_sides(difference=dict(moved, noiseRatio=1.5)))],
                                     contract)
        self.assertFalse(report['outputs']['held'])

    def test_sides_outside_the_across_contract_refuse_the_timing(self) -> None:
        for difference in ({'correlation': 0.99}, {'noiseRatio': 1.5}, {'locationShiftInNoise': -0.2},
                           {'p99AbsoluteDifferenceInNoise': 3.0}, {'sameShape': False}):
            report = measurement.compare([self.write(f'x{len(str(difference))}{list(difference)[0]}',
                                                     self.differing_sides(difference=difference))], self.contract())
            self.assertFalse(report['outputs']['held'], difference)

    def test_differing_sides_without_a_sample_comparison_refuse_the_timing(self) -> None:
        record = self.differing_sides()
        del record['acrossSides']
        report = measurement.compare([self.write('a', record)], self.contract())
        self.assertFalse(report['outputs']['held'])
        self.assertTrue(any('never compared sample by sample' in violation
                            for violation in report['outputs']['violations']), report['outputs']['violations'])
        record = self.differing_sides()
        for entry in record['runs']:
            entry.pop('outputAgreement', None)
        report = measurement.compare([self.write('b', record)], self.contract())
        self.assertFalse(report['outputs']['held'])

    def test_a_pair_whose_sides_published_other_roles_refuses_the_timing(self) -> None:
        # Both first runs published both roles and were compared; one later candidate run left the bias master out.
        record = self.differing_sides(roles=('bias-master', 'light-master'))
        for entry in record['runs']:
            if entry['side'] == 'candidate' and entry['pair'] == 5:
                entry['outputs'].pop('bias-master:bias-master.fit')
                entry['outputAgreement'].pop('bias-master:bias-master.fit')
        report = measurement.compare([self.write('a', record)], self.contract())
        self.assertFalse(report['outputs']['held'])
        self.assertEqual(len(report['outputs']['violations']), 1, report['outputs']['violations'])
        self.assertIn('pair 5', report['outputs']['violations'][0])

    def test_a_role_published_and_never_compared_across_the_sides_refuses_the_timing(self) -> None:
        record = self.differing_sides(roles=('bias-master', 'light-master'))
        record['acrossSides']['variant0'].pop('light-master:light-master.fit')
        report = measurement.compare([self.write('a', record)], self.contract())
        self.assertEqual(report['outputs']['violations'],
                         ['light-master was published but not compared across the sides'])

    def test_a_role_one_side_did_not_publish_refuses_the_timing(self) -> None:
        record = self.differing_sides(roles=('bias-master', 'light-master'))
        for entry in record['runs']:
            if entry['side'] == 'candidate':
                entry['outputs'].pop('bias-master:bias-master.fit')
                entry.get('outputAgreement', {}).pop('bias-master:bias-master.fit', None)
        record['acrossSides']['variant0'].pop('bias-master:bias-master.fit')
        report = measurement.compare([self.write('a', record)], self.contract())
        self.assertFalse(report['outputs']['held'])

    def test_a_run_that_did_not_complete_refuses_the_comparison(self) -> None:
        report = measurement.compare([self.write('a', runs_record(self.pairs(success=False)))], self.configuration)
        self.assertTrue(report['verdict'].startswith('refused'))

    def test_an_incremental_case_must_have_reused_everything_before_the_light_stack(self) -> None:
        fresh = measurement.compare([self.write('a', runs_record(self.pairs(), 'incremental'))], self.configuration)
        self.assertIn('did not reuse', fresh['verdict'])

        # The full process plans the reuse; Lite's direct preparation plans a full run and its executor reuses
        # every artifact that is still valid. Either way the light stack alone ran again.
        planned = self.pairs(mode='incremental', stages=['LightsNormalization', 'LightsStacking'])
        for entry in planned:
            entry['executionMode'] = 'incremental'
            entry['stages'] = ['LightsNormalization', 'LightsStacking']
        report = measurement.compare([self.write('b', runs_record(planned, 'incremental'))], self.configuration)
        self.assertEqual(report['verdict'], 'no regression')
        reused = ['LightsRegistration', 'LightsAlignment', 'LightsNormalization', 'LightsStacking']
        direct = self.pairs(stages=reused)
        for entry in direct:
            entry['stages'] = list(reused)
        report = measurement.compare([self.write('c', runs_record(direct, 'incremental'))], self.configuration)
        self.assertEqual(report['verdict'], 'no regression')

        unstacked = self.pairs(stages=reused[:-1])
        for entry in unstacked:
            entry['stages'] = reused[:-1]
        report = measurement.compare([self.write('d', runs_record(unstacked, 'incremental'))], self.configuration)
        self.assertIn('did not reuse', report['verdict'])

        # A variant the settings never reached leaves the stack valid: the stage runs, reuses it and publishes nothing.
        unchanged = self.pairs(stages=reused, outputs={})
        for entry in unchanged:
            entry['stages'] = list(reused)
            entry['outputs'] = {}
        report = measurement.compare([self.write('e', runs_record(unchanged, 'incremental'))], self.configuration)
        self.assertIn('did not reuse', report['verdict'])

    def test_two_batches_are_analysed_together(self) -> None:
        first = self.write('a', runs_record(self.pairs()))
        second = self.write('b', runs_record(self.pairs()))
        report = measurement.compare([first, second], self.configuration)
        self.assertEqual((report['batches'], report['pairs']), (2, 24))
        other = runs_record(self.pairs())
        other['case'] = 'cold'
        with self.assertRaises(measurement.MeasurementError):
            measurement.compare([first, self.write('c', other)], self.configuration)

    def test_the_cancellation_case_reports_latency_and_decides_no_throughput(self) -> None:
        runs = [run(pair * 2 + 1, side, pair, 20000.0, 1, success=False, cancelled=True, latency=8.0)
                for pair in range(12) for side in ('baseline', 'candidate')]
        report = measurement.compare([self.write('a', runs_record(runs, 'cancel'))], self.configuration)
        self.assertEqual(report['verdict'], 'reported')
        self.assertIn('cancellationLatencyMs', report)
        self.assertNotIn('wallTime', report)

    def test_warmups_and_primes_are_not_measured(self) -> None:
        report = measurement.compare([self.write('a', runs_record(self.pairs(count=3)))], self.configuration)
        self.assertEqual(report['pairs'], 3)
        self.assertNotIn(999.0, report['wallTime']['ratios'])


def write_fits(path: pathlib.Path, samples, bitpix=-32, cards=()):
    """A primary image the way a FITS writer lays it out: 2880-byte header blocks, big-endian samples, padding."""
    import numpy
    samples = numpy.asarray(samples)
    lines = ['SIMPLE  =                    T', f'BITPIX  = {bitpix:>20}', f'NAXIS   = {samples.ndim:>20}']
    for axis, length in enumerate(reversed(samples.shape), start=1):
        lines.append(f'NAXIS{axis}  = {length:>20}')
    lines += list(cards) + ['END']
    header = ''.join(line.ljust(80) for line in lines).encode('ascii')
    header += b' ' * (-len(header) % 2880)
    dtype = {-32: '>f4', -64: '>f8', 16: '>i2', 8: 'u1', 32: '>i4'}[bitpix]
    data = samples.astype(dtype).tobytes()
    data += b'\0' * (-len(data) % 2880)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + data)
    return path


class OutputReadingTest(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = pathlib.Path(self.directory.name)

    def test_a_float_image_is_read_top_down_as_written(self) -> None:
        path = write_fits(self.root / 'a.fit', [[0.0, 0.25], [0.5, 1.0], [0.125, 0.75]])
        samples, cards = measurement.read_fits_image(path)
        self.assertEqual(samples.shape, (1, 3, 2))
        self.assertEqual(samples[0, 1, 0], 0.5)
        self.assertEqual(cards['BITPIX'], -32)

    def test_a_bottom_up_image_is_turned_to_top_down(self) -> None:
        path = write_fits(self.root / 'b.fit', [[1.0, 2.0], [3.0, 4.0]], cards=["ROWORDER= 'BOTTOM-UP'"])
        samples, _ = measurement.read_fits_image(path)
        self.assertEqual(samples[0, 0].tolist(), [3.0, 4.0])

    def test_unsigned_codes_are_read_through_their_zero_point_as_fractions_of_full_scale(self) -> None:
        path = write_fits(self.root / 'c.fit', [[-32768, 32767]], bitpix=16,
                          cards=['BZERO   =                32768', 'BSCALE  =                    1'])
        samples, cards = measurement.read_fits_image(path)
        self.assertEqual(samples[0, 0].tolist(), [0.0, 65535.0])
        self.assertEqual(measurement.as_fractions(samples, cards)[0, 0].tolist(), [0.0, 1.0])
        float_samples, float_cards = measurement.read_fits_image(write_fits(self.root / 'f.fit', [[0.5]]))
        self.assertEqual(measurement.as_fractions(float_samples, float_cards)[0, 0].tolist(), [0.5])

    def test_a_three_plane_image_keeps_its_planes(self) -> None:
        import numpy
        path = write_fits(self.root / 'd.fit', numpy.arange(24, dtype=float).reshape(3, 2, 4))
        samples, _ = measurement.read_fits_image(path)
        self.assertEqual(samples.shape, (3, 2, 4))
        self.assertEqual(samples[2, 1, 3], 23.0)

    def test_a_file_that_is_not_fits_is_refused(self) -> None:
        path = self.root / 'e.fit'
        path.write_bytes(b'not a fits file' * 300)
        with self.assertRaises(measurement.MeasurementError):
            measurement.read_fits_image(path)


class SampleAgreementTest(unittest.TestCase):
    def test_identical_samples_agree_exactly(self) -> None:
        import numpy
        a = numpy.linspace(0.0, 1.0, 1000, dtype=numpy.float32)
        agreement = measurement.sample_agreement(a, a.copy())
        self.assertTrue(agreement['identical'])
        self.assertEqual((agreement['differingSamples'], agreement['maxUlp']), (0, 0))

    def test_a_sample_one_unit_in_the_last_place_away_is_counted(self) -> None:
        import numpy
        a = numpy.full(100, 0.3, dtype=numpy.float32)
        b = a.copy()
        b[7] = numpy.nextafter(b[7], numpy.float32(1.0))
        b[9] = numpy.nextafter(numpy.nextafter(b[9], numpy.float32(0.0)), numpy.float32(0.0))
        agreement = measurement.sample_agreement(a, b)
        self.assertFalse(agreement['identical'])
        self.assertEqual(agreement['differingSamples'], 2)
        self.assertEqual(agreement['samples'], 100)
        self.assertEqual(agreement['maxUlp'], 2)

    def test_the_ulp_distance_crosses_zero_without_wrapping(self) -> None:
        import numpy
        a = numpy.array([numpy.float32(-0.0), numpy.float32(1e-45)], dtype=numpy.float32)
        b = numpy.array([numpy.float32(1e-45), numpy.float32(-1e-45)], dtype=numpy.float32)
        self.assertEqual(measurement.sample_agreement(a, b)['maxUlp'], 2)

    def test_samples_of_another_shape_do_not_agree(self) -> None:
        import numpy
        agreement = measurement.sample_agreement(numpy.zeros((1, 2, 3)), numpy.zeros((1, 3, 2)))
        self.assertFalse(agreement['identical'])
        self.assertFalse(agreement['sameShape'])

    def test_two_nans_in_one_place_agree(self) -> None:
        import numpy
        a = numpy.array([numpy.nan, 1.0], dtype=numpy.float32)
        self.assertTrue(measurement.sample_agreement(a, a.copy())['identical'])


class OutputKeeperTest(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = pathlib.Path(self.directory.name)
        self.keeper = measurement.OutputKeeper(self.root / 'kept')

    def result(self, name, light, bias=None, success=True):
        """A driver result whose outputs were written under `name`."""
        out = self.root / name / 'out'
        outputs = [{'role': 'light-master', 'path': 'stack.fit',
                    'sha256': measurement.file_sha256(write_fits(out / 'stack.fit', light))},
                   {'role': 'report', 'path': 'history/report.html', 'sha256': 'r' * 64}]
        (out / 'history').mkdir(parents=True, exist_ok=True)
        (out / 'history/report.html').write_text('report', encoding='utf-8')
        if bias is not None:
            outputs.append({'role': 'bias-master', 'path': 'master_bias.fit',
                            'sha256': measurement.file_sha256(write_fits(out / 'master_bias.fit', bias))})
        return {'outcome': {'success': success}, 'outputs': outputs}, out

    def test_a_sides_first_run_is_kept_and_its_later_runs_are_compared_with_it(self) -> None:
        import numpy
        light = numpy.full((4, 5), 0.3, dtype=numpy.float32)
        first, out = self.result('a', light, bias=[[0.01, 0.02]])
        self.assertEqual(self.keeper.take('baseline', 0, 1, first, out), {'outputReference': True})
        # The kept copy survives the run's output being deleted.
        for path in sorted(out.rglob('*'), reverse=True):
            path.unlink() if path.is_file() else path.rmdir()
        moved = light.copy()
        moved[2, 3] = numpy.nextafter(moved[2, 3], numpy.float32(1.0))
        second, out = self.result('b', moved, bias=[[0.01, 0.02]])
        fields = self.keeper.take('baseline', 0, 3, second, out)
        agreements = fields['outputAgreement']
        self.assertEqual(sorted(agreements), ['bias-master:master_bias.fit', 'light-master:stack.fit'])
        self.assertTrue(agreements['bias-master:master_bias.fit']['identical'])
        self.assertEqual(agreements['light-master:stack.fit']['maxUlp'], 1)
        self.assertEqual(agreements['light-master:stack.fit']['differingSamples'], 1)
        self.assertEqual(agreements['light-master:stack.fit']['reference'], 1)

    def test_an_image_with_its_first_runs_digest_is_not_read_again(self) -> None:
        import numpy
        light = numpy.full((4, 5), 0.3, dtype=numpy.float32)
        first, out = self.result('a', light)
        self.keeper.take('baseline', 0, 1, first, out)
        # The kept copy is made unreadable: an equal digest decides without reading it.
        for kept in (self.root / 'kept').rglob('*.fit'):
            kept.write_bytes(b'not a fits file')
        second, out = self.result('b', light)
        agreement = self.keeper.take('baseline', 0, 2, second, out)['outputAgreement']['light-master:stack.fit']
        self.assertTrue(agreement['identical'])
        self.assertTrue(agreement['byDigest'])

    def test_each_variant_has_its_own_first_run(self) -> None:
        import numpy
        zero, out = self.result('a', numpy.zeros((2, 2), dtype=numpy.float32))
        self.keeper.take('candidate', 0, 1, zero, out)
        one, out = self.result('b', numpy.ones((2, 2), dtype=numpy.float32))
        self.assertEqual(self.keeper.take('candidate', 1, 2, one, out), {'outputReference': True})

    def test_a_run_that_did_not_complete_is_neither_kept_nor_compared(self) -> None:
        import numpy
        failed, out = self.result('a', numpy.zeros((2, 2), dtype=numpy.float32), success=False)
        self.assertEqual(self.keeper.take('baseline', 0, 1, failed, out), {})
        later, out = self.result('b', numpy.zeros((2, 2), dtype=numpy.float32))
        self.assertEqual(self.keeper.take('baseline', 0, 2, later, out), {'outputReference': True})

    def test_the_sides_first_runs_are_described_against_each_other_per_variant(self) -> None:
        import numpy
        generator = numpy.random.default_rng(3)
        base = (0.1 + 0.01 * generator.standard_normal((30, 40))).astype(numpy.float32)
        b, out = self.result('a', base, bias=[[0.01, 0.02]])
        self.keeper.take('baseline', 0, 1, b, out)
        c, out = self.result('b', base + numpy.float32(0.001), bias=[[0.01, 0.02]])
        self.keeper.take('candidate', 0, 2, c, out)
        lone, out = self.result('c', base)
        self.keeper.take('baseline', 1, 3, lone, out)
        across = self.keeper.across()
        self.assertEqual(sorted(across), ['variant0'], 'a variant one side alone measured is not compared')
        light = across['variant0']['light-master:stack.fit']
        self.assertAlmostEqual(light['locationShiftInNoise'], 0.1, delta=0.01)
        self.assertEqual(light['candidate'], 'light-master:stack.fit')
        self.assertEqual(across['variant0']['bias-master:master_bias.fit']['medianAbsoluteDifferenceInNoise'], 0.0)

    def test_a_role_published_under_another_name_is_matched_by_its_role(self) -> None:
        import numpy
        b, out = self.result('a', numpy.zeros((2, 2), dtype=numpy.float32))
        self.keeper.take('baseline', 0, 1, b, out)
        c, out = self.result('b', numpy.zeros((2, 2), dtype=numpy.float32))
        (out / 'stack.fit').rename(out / 'other.fit')
        c['outputs'][0]['path'] = 'other.fit'
        self.keeper.take('candidate', 0, 2, c, out)
        light = self.keeper.across()['variant0']['light-master:stack.fit']
        self.assertEqual(light['candidate'], 'light-master:other.fit')


class OutputDifferenceTest(unittest.TestCase):
    def test_the_statistics_describe_a_shift_and_a_noise_in_the_baselines_noise(self) -> None:
        import numpy
        generator = numpy.random.default_rng(5)
        baseline = 0.1 + 0.01 * generator.standard_normal((1, 300, 400))
        candidate = baseline + 0.001
        statistics = measurement.output_difference(baseline, candidate)
        self.assertTrue(statistics['sameShape'])
        self.assertAlmostEqual(statistics['locationShiftInNoise'], 0.1, delta=0.01)
        self.assertAlmostEqual(statistics['noiseRatio'], 1.0, delta=1e-6)
        self.assertAlmostEqual(statistics['medianAbsoluteDifferenceInNoise'], 0.1, delta=0.01)
        self.assertGreater(statistics['correlation'], 0.999)
        self.assertEqual(statistics['nonFinite'], [0, 0])

    def test_unrelated_images_do_not_correlate(self) -> None:
        import numpy
        generator = numpy.random.default_rng(6)
        statistics = measurement.output_difference(generator.random((1, 50, 60)), generator.random((1, 50, 60)))
        self.assertLess(abs(statistics['correlation']), 0.1)

    def test_images_on_other_grids_are_described_by_their_covered_samples(self) -> None:
        import numpy
        generator = numpy.random.default_rng(7)
        baseline = numpy.zeros((1, 60, 50))
        baseline[0, 5:55, 5:45] = 0.2 + 0.01 * generator.standard_normal((50, 40))
        candidate = 0.21 + 0.02 * generator.standard_normal((1, 40, 70))
        statistics = measurement.output_difference(baseline, candidate)
        self.assertFalse(statistics['sameShape'])
        self.assertEqual(statistics['coveredSamples'], [2000, 2800])
        self.assertAlmostEqual(statistics['locationShiftInNoise'], 1.0, delta=0.15)
        self.assertAlmostEqual(statistics['noiseRatio'], 2.0, delta=0.2)
        self.assertIsNone(statistics['correlation'])
        self.assertIsNone(statistics['medianAbsoluteDifferenceInNoise'])


class FakeRule:
    def __init__(self, excludes):
        self.excludes = excludes

    def is_excluded(self, name):
        return any(pathlib.PurePath(name).match(pattern) for pattern in self.excludes)


class ArchiveTest(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.bin = pathlib.Path(self.directory.name) / 'bin'
        for name in ('AfterNight.exe', 'AfterNight.pdb', 'AfterNightPlatform.dll', 'AfterNightPlatform.pdb',
                     'Test_Platform.exe', 'Test_Platform.pdb',
                     'AfterNightWorkflowDriver.exe', 'AfterNightWorkflowDriver.pdb',
                     'Qt6Core.dll', 'platforms/qoffscreen.dll', 'run.log'):
            path = self.bin / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())
        self.rule = FakeRule(['*.pdb', '*.log', 'Test_*', 'AfterNightWorkflowDriver*'])

    def test_the_selection_is_the_payload_the_driver_and_first_party_symbols(self) -> None:
        selected = {path.relative_to(self.bin).as_posix() for path in
                    measurement.archive_selection(self.bin, self.rule, 'windows')}
        self.assertEqual(selected, {'AfterNight.exe', 'AfterNight.pdb', 'AfterNightPlatform.dll',
                                    'AfterNightPlatform.pdb',
                                    'AfterNightWorkflowDriver.exe', 'AfterNightWorkflowDriver.pdb', 'Qt6Core.dll',
                                    'platforms/qoffscreen.dll'})

    def test_the_selection_holds_the_drivers_import_closure_where_the_payload_leaves_it_out(self) -> None:
        # The driver loads libraries the packager's payload leaves out; the archive needs them to run it at all.
        for name in ('AfterNightEngine.dll', 'AfterNightEngine.pdb', 'AfterNightControls.dll',
                     'AfterNightControls.pdb', 'kddockwidgets-qt6.dll', 'Unrelated.dll'):
            (self.bin / name).write_bytes(name.encode())
        imports = {'afternightworkflowdriver.exe': ['AfterNightControls.dll', 'KERNEL32.dll', 'qt6core.dll'],
                   'afternightcontrols.dll': ['AFTERNIGHTENGINE.dll', 'kddockwidgets-qt6.dll'],
                   'afternightengine.dll': ['KERNEL32.dll'], 'kddockwidgets-qt6.dll': ['Qt6Core.dll'],
                   'qt6core.dll': []}
        rule = FakeRule(['*.pdb', '*.log', 'Test_*', 'AfterNightWorkflowDriver*', 'AfterNightEngine*',
                         'AfterNightControls*', 'kddockwidgets*', 'Unrelated*'])
        read = []

        def reader(path):
            read.append(path.name)
            return imports.get(path.name.lower())
        selected = {path.relative_to(self.bin).as_posix() for path in
                    measurement.archive_selection(self.bin, rule, 'windows', reader)}
        self.assertEqual(selected, {'AfterNight.exe', 'AfterNight.pdb', 'AfterNightPlatform.dll',
                                    'AfterNightPlatform.pdb', 'AfterNightWorkflowDriver.exe',
                                    'AfterNightWorkflowDriver.pdb', 'Qt6Core.dll',
                                    'platforms/qoffscreen.dll', 'AfterNightControls.dll', 'AfterNightControls.pdb',
                                    'AfterNightEngine.dll', 'AfterNightEngine.pdb', 'kddockwidgets-qt6.dll'})
        self.assertEqual(len(read), len(set(read)), 'each binary is read once')
        self.assertEqual(measurement.engine_library(selected), 'AfterNightEngine.dll')
        self.assertIsNone(measurement.engine_library({'AfterNightPlatform.dll', 'Qt6Core.dll'}))
        self.assertEqual(measurement.engine_library({'lib/libAfterNightEngine.so', 'libQt6Core.so.6'}),
                         'libAfterNightEngine.so')

    def test_an_archive_is_held_to_its_manifest(self) -> None:
        archive = pathlib.Path(self.directory.name) / 'archive'
        files = []
        for path in measurement.archive_selection(self.bin, self.rule, 'windows'):
            target = archive / 'bin' / path.relative_to(self.bin)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(path.read_bytes())
            files.append({'path': 'bin/' + path.relative_to(self.bin).as_posix(), 'bytes': target.stat().st_size,
                          'sha256': measurement.file_sha256(target)})
        (archive / measurement.ARCHIVE_MANIFEST).write_text(json.dumps({'files': files}), encoding='utf-8')
        self.assertEqual(measurement.verify_archive(archive), [])
        (archive / 'bin/Qt6Core.dll').write_bytes(b'changed')
        (archive / 'bin/extra.dll').write_bytes(b'extra')
        (archive / 'bin/AfterNight.pdb').unlink()
        problems = measurement.verify_archive(archive)
        self.assertIn('bin/Qt6Core.dll differs from its record', problems)
        self.assertIn('bin/extra.dll is not in the manifest', problems)
        self.assertIn('bin/AfterNight.pdb is missing', problems)

    def test_the_linker_version_is_read_from_the_pe_header(self) -> None:
        header = bytearray(512)
        header[0:2] = b'MZ'
        struct.pack_into('<I', header, 0x3C, 0x80)
        header[0x80:0x84] = b'PE\0\0'
        header[0x80 + 24 + 2] = 14
        header[0x80 + 24 + 3] = 51
        path = pathlib.Path(self.directory.name) / 'fake.dll'
        path.write_bytes(bytes(header))
        self.assertEqual(measurement.pe_linker_version(path), '14.51')
        self.assertIsNone(measurement.pe_linker_version(self.bin / 'run.log'))

    def test_a_linux_selection_keeps_every_first_party_binarys_symbols(self) -> None:
        tree = pathlib.Path(self.directory.name) / 'linux'
        for name in ('AfterNight', 'AfterNight.debug', 'libAfterNightPlatform.so', 'libAfterNightPlatform.so.debug',
                     'liban_extension_runtime_backend.so', 'liban_extension_runtime_backend.so.debug',
                     'AfterNightWorkflowDriver', 'AfterNightWorkflowDriver.debug',
                     'Test_Platform', 'Test_Platform.debug',
                     'libQt6Core.so.6', 'libQt6Core.so.6.debug', 'run.log'):
            path = tree / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())
        # The AppImage packager copies the application by a rule of its own and leaves it out of its library copy.
        rule = FakeRule(['AfterNight', '*.debug', '*.log', 'Test_*', 'AfterNightWorkflowDriver*'])
        selected = {path.name for path in measurement.archive_selection(tree, rule, 'linux')}
        self.assertEqual(selected, {'AfterNight', 'AfterNight.debug', 'libAfterNightPlatform.so',
                                    'libAfterNightPlatform.so.debug', 'liban_extension_runtime_backend.so',
                                    'liban_extension_runtime_backend.so.debug', 'AfterNightWorkflowDriver',
                                    'AfterNightWorkflowDriver.debug', 'libQt6Core.so.6'})

    def test_the_compilers_are_read_from_an_elf_comment_section(self) -> None:
        comment = b'GCC: (Ubuntu 15.2.0-16ubuntu1) 15.2.0\0Ubuntu clang version 21.1.8 (6ubuntu1)\0'
        names = b'\0.shstrtab\0.comment\0'
        data_offset = 64
        names_offset = data_offset + len(comment)
        table_offset = (names_offset + len(names) + 7) // 8 * 8
        image = bytearray(table_offset + 3 * 64)
        image[0:6] = b'\x7fELF\x02\x01'
        struct.pack_into('<Q', image, 0x28, table_offset)
        struct.pack_into('<HHH', image, 0x3A, 64, 3, 1)
        struct.pack_into('<IIQQQQ', image, table_offset + 64, 1, 3, 0, 0, names_offset, len(names))
        struct.pack_into('<IIQQQQ', image, table_offset + 128, 11, 1, 0, 0, data_offset, len(comment))
        image[data_offset:data_offset + len(comment)] = comment
        image[names_offset:names_offset + len(names)] = names
        tree = pathlib.Path(self.directory.name) / 'elf'
        tree.mkdir()
        for name in ('libAfterNightPlatform.so', 'AfterNight'):
            (tree / name).write_bytes(bytes(image))
        (tree / 'libQt6Core.so.6').write_bytes(bytes(image).replace(b'21.1.8', b'19.0.0'))
        self.assertEqual(measurement.elf_comment_strings(tree / 'AfterNight'),
                         ['GCC: (Ubuntu 15.2.0-16ubuntu1) 15.2.0', 'Ubuntu clang version 21.1.8 (6ubuntu1)'])
        self.assertEqual(measurement.elf_comment_strings(self.bin / 'run.log'), [])
        toolchain = measurement.binary_toolchain(sorted(tree.iterdir()))
        self.assertEqual(toolchain['compilers'],
                         ['GCC: (Ubuntu 15.2.0-16ubuntu1) 15.2.0', 'Ubuntu clang version 21.1.8 (6ubuntu1)'])
        self.assertEqual(toolchain['linker'], [])
        self.assertEqual(measurement.binary_toolchain([self.bin / 'AfterNight.exe'])['compilers'], [])

    def test_the_qt_version_is_the_assignment_not_a_path_that_uses_it(self) -> None:
        common = ('      qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/include")\n'
                  'AfterNightCommon.QT_VERSION = "6.10.3"\n'
                  '        return "../../Qt6/" .. AfterNightCommon.QT_VERSION .. "/"\n')
        self.assertEqual(measurement.qt_version(common), '6.10.3')
        self.assertEqual(measurement.qt_version('local QT_VERSION = "6.9.1"\n'), '6.9.1')
        self.assertEqual(measurement.qt_version('qtpath("../../Qt6/" .. QT_VERSION)\n'), '')
        repository = pathlib.Path(__file__).resolve().parents[3] / 'build/premake/afternight-common.lua'
        self.assertRegex(measurement.qt_version(repository.read_text(encoding='utf-8')), r'^\d+\.\d+\.\d+$')


class HostTest(unittest.TestCase):
    def test_a_child_sees_its_deployment_and_the_system_only(self) -> None:
        os.environ['AFTERNIGHT_TEST_STORAGE_ROOT'] = 'somewhere'
        self.addCleanup(os.environ.pop, 'AFTERNIGHT_TEST_STORAGE_ROOT', None)
        env = measurement.child_environment(pathlib.Path('deployment'))
        self.assertNotIn('AFTERNIGHT_TEST_STORAGE_ROOT', env)
        variable = 'PATH' if measurement.host_platform() == 'windows' else 'LD_LIBRARY_PATH'
        self.assertTrue(env[variable].startswith('deployment'))
        if measurement.host_platform() == 'windows':
            self.assertEqual(len(env['PATH'].split(os.pathsep)), 3)

    def test_eviction_reads_every_file_it_is_given(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            paths = []
            for index in range(3):
                path = pathlib.Path(directory) / f'{index}.fits'
                path.write_bytes(b'x' * 8192)
                paths.append(path)
            self.assertGreaterEqual(measurement.evict_from_cache(paths), 0.0)

    def test_the_host_load_is_a_fraction(self) -> None:
        busy = measurement.host_busy_fraction(0.1)
        if busy is not None:
            self.assertGreaterEqual(busy, 0.0)
            self.assertLessEqual(busy, 1.0)


class ConfigurationTest(unittest.TestCase):
    def setUp(self) -> None:
        self.configuration = measurement.read_configuration()

    def test_the_repository_configuration_holds(self) -> None:
        self.assertEqual(measurement.check(measurement.REPO_ROOT), [])

    def assertFinding(self, change, text) -> None:
        configuration = copy.deepcopy(self.configuration)
        change(configuration)
        problems = measurement.configuration_findings(configuration)
        self.assertTrue(any(text in problem for problem in problems), f'no problem contains {text!r}: {problems}')

    def test_the_protocol_is_the_tools_own(self) -> None:
        self.assertFinding(lambda c: c['protocol'].update(pairs=8), 'the protocol pairs is 8')
        self.assertFinding(lambda c: c['protocol'].update(noRegressionRatio=1.05), 'noRegressionRatio')
        self.assertFinding(lambda c: c['protocol'].update(resolutionFactor=1.5), 'resolutionFactor')
        self.assertFinding(lambda c: c['protocol'].update(samplingIntervalMs=100), 'samplingIntervalMs')
        self.assertFinding(lambda c: c['protocol'].pop('seed'), 'no seed')

    def test_the_dataset_is_pinned_whole(self) -> None:
        self.assertFinding(lambda c: c['dataset'].pop('identityDigest'), 'no identityDigest')
        self.assertFinding(lambda c: c['dataset'].update(identityDigest='abc'), 'not a SHA-256')
        self.assertFinding(lambda c: c['dataset']['roles'].update(lights=71), 'do not add up')

    def test_the_cases_and_workflows_are_exactly_the_protocols(self) -> None:
        self.assertFinding(lambda c: c['cases'].pop('cold'), 'the cases must be exactly')
        self.assertFinding(lambda c: c['workflows'].pop('lite'), 'the workflows must be exactly')
        self.assertFinding(lambda c: c['cases']['cancel'].pop('cancelAfterMs'), 'no cancelAfterMs')
        self.assertFinding(lambda c: c['hosts'].pop('bench-linux'), 'no host entry for bench-linux')

    def test_the_check_holds_the_campaign_to_its_budget(self) -> None:
        self.assertFinding(lambda c: c.pop('campaign'), 'states no campaign')
        self.assertFinding(lambda c: c['campaign'].update(budgetMinutes=5), 'over its budget')

    def test_the_output_contract_bounds_every_published_role(self) -> None:
        self.assertFinding(lambda c: c.pop('outputContract'), 'states no output contract')
        self.assertFinding(lambda c: c['outputContract']['acrossSides'].pop('flat-master'),
                           'states no accepted difference for flat-master')
        self.assertFinding(lambda c: c['outputContract']['acrossSides']['bias-master'].pop('minCorrelation'),
                           'bias-master bound states no minCorrelation')
        self.assertFinding(lambda c: c['outputContract']['acrossSides']['dark-master'].update(noiseRatio=[1.1, 1.2]),
                           'dark-master noiseRatio')
        self.assertFinding(lambda c: c['outputContract']['acrossSides']['light-master'].update(grid='round'),
                           'light-master grid')
        self.assertFinding(lambda c: c['outputContract']['withinSide']['light-master'].update(maxUlp=-1),
                           'light-master maxUlp')
        self.assertFinding(lambda c: c['outputContract']['withinSide']['light-master'].update(maxDifferingShare=2),
                           'light-master maxDifferingShare')


class CampaignTest(unittest.TestCase):
    def setUp(self) -> None:
        self.configuration = copy.deepcopy(measurement.read_configuration())
        self.configuration['hosts']['bench-windows']['runSeconds'] = {
            'baseline': {'fresh': 80, 'incremental': 20, 'cancel': 30},
            'candidate': {'fresh': 100, 'incremental': 40, 'cancel': 30}}
        self.configuration['campaign']['hosts']['bench-windows'] = {
            'pairs': 3, 'series': ['full-warm', 'full-incremental', 'full-cancel', 'lite-cold']}

    def test_every_run_a_series_makes_is_counted(self) -> None:
        self.assertEqual(measurement.series_runs('warm', 3, 0), [('baseline', 'fresh'), ('candidate', 'fresh')] * 3)
        incremental = measurement.series_runs('incremental', 2, 1)
        self.assertEqual(incremental[:2], [('baseline', 'fresh'), ('candidate', 'fresh')])
        self.assertEqual(incremental[2:], [('baseline', 'incremental'), ('candidate', 'incremental')] * 3)
        self.assertEqual(measurement.series_runs('cancel', 2, 0), [('baseline', 'cancel'), ('candidate', 'cancel')] * 2)

    def test_the_estimate_adds_each_sides_runs_the_overhead_and_the_eviction(self) -> None:
        estimate = measurement.campaign_estimate(self.configuration, 'bench-windows')
        overhead = measurement.RUN_OVERHEAD_SECONDS
        self.assertEqual(estimate['series']['full-warm'], 3 * (80 + 100 + 2 * overhead))
        self.assertEqual(estimate['series']['full-incremental'], 80 + 100 + 3 * (20 + 40) + 8 * overhead)
        self.assertEqual(estimate['series']['full-cancel'], 3 * (30 + 30 + 2 * overhead))
        self.assertEqual(estimate['series']['lite-cold'],
                         3 * (80 + 100 + 2 * overhead + 2 * measurement.EVICTION_SECONDS))
        self.assertEqual(estimate['totalSeconds'], sum(estimate['series'].values()))

    def test_a_campaign_within_its_budget_holds(self) -> None:
        self.assertEqual(measurement.campaign_findings(self.configuration), [])

    def test_a_campaign_over_its_budget_is_refused(self) -> None:
        self.configuration['campaign']['hosts']['bench-windows']['series'] += ['lite-warm', 'full-cold']
        problems = measurement.campaign_findings(self.configuration)
        self.assertTrue(any('over its budget' in problem for problem in problems), problems)

    def test_a_campaign_is_bounded_to_an_hour_and_the_protocols_pairs(self) -> None:
        for change, text in ((lambda c: c['campaign'].update(budgetMinutes=90), 'up to an hour'),
                             (lambda c: c['campaign']['hosts']['bench-windows'].update(pairs=12), 'pairs'),
                             (lambda c: c['campaign']['hosts']['bench-windows'].update(pairs=1), 'pairs'),
                             (lambda c: c['campaign']['hosts']['bench-windows'].update(series=['full-hot']), 'full-hot'),
                             (lambda c: c['campaign']['hosts'].pop('bench-linux'), 'nothing for bench-linux'),
                             (lambda c: c['hosts']['bench-linux'].pop('runSeconds'), 'run seconds'),
                             (lambda c: c.pop('campaign'), 'no campaign')):
            configuration = copy.deepcopy(self.configuration)
            change(configuration)
            problems = measurement.campaign_findings(configuration)
            self.assertTrue(any(text in problem for problem in problems), (text, problems))

    def test_the_repository_campaigns_are_within_their_budget(self) -> None:
        configuration = measurement.read_configuration()
        for host in ('bench-windows', 'bench-linux'):
            estimate = measurement.campaign_estimate(configuration, host)['totalSeconds']
            self.assertLessEqual(estimate, configuration['campaign']['budgetMinutes'] * 60, host)

    def test_a_run_longer_than_the_protocol_is_refused_unless_asked(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / 'out'
            arguments = ['run', '--baseline', directory, '--candidate', directory, '--workflow', 'full', '--case',
                         'warm', '--host-id', 'bench-windows', '--corpus-root', directory, '--output', str(output),
                         '--pairs', str(measurement.PROTOCOL['pairs'] + 1)]
            with unittest.mock.patch('builtins.print') as printed:
                self.assertEqual(measurement.main(arguments), 1)
            self.assertIn('--over-budget', ' '.join(str(call.args[0]) for call in printed.call_args_list))

    def test_a_campaign_dry_run_prints_its_plan_and_runs_nothing(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            with unittest.mock.patch('builtins.print') as printed:
                self.assertEqual(measurement.main(['campaign', '--baseline', directory, '--candidate', directory,
                                                   '--host-id', 'bench-windows', '--corpus-root', directory,
                                                   '--output', str(pathlib.Path(directory) / 'out'), '--dry-run']), 0)
            text = ' '.join(str(call.args[0]) for call in printed.call_args_list)
            self.assertIn('full-warm', text)
            self.assertFalse((pathlib.Path(directory) / 'out').exists())


class BaselineTest(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = pathlib.Path(self.directory.name)
        git(self.root, 'init', '-q')
        git(self.root, 'config', 'user.email', 'measurement@example.invalid')
        git(self.root, 'config', 'user.name', 'Measurement')
        (self.root / 'README').write_text('x', encoding='utf-8')
        git(self.root, 'add', '-A')
        git(self.root, 'commit', '-q', '-m', 'base')
        self.commit = git(self.root, 'rev-parse', 'HEAD')
        self.configuration = measurement.read_configuration()
        identity = self.configuration['dataset']['identityDigest']
        records = self.root / 'records'
        records.mkdir()
        (records / 'archive-manifest.json').write_text(json.dumps({'commit': self.commit,
                                                                   'dataset': {'identityDigest': identity}}))
        (records / 'full-warm.json').write_text(json.dumps({
            'datasetIdentity': identity, 'pairs': 12,
            'wallTime': {'medianRatio': 0.995}, 'residentPeak': {'medianRatio': 1.0001}}))
        (records / 'full-cancel.json').write_text(json.dumps({
            'datasetIdentity': identity, 'pairs': 12, 'cancellationLatencyMs': {'medianRatio': 2.0}}))
        self.entry = {'host': 'bench-windows', 'commit': self.commit, 'archiveManifest': 'records/archive-manifest.json',
                      'records': ['records/full-warm.json', 'records/full-cancel.json'],
                      'resolution': {'wallTime': 1.01, 'residentPeak': 1.0002}}

    def findings(self, **changes) -> list[str]:
        entry = dict(self.entry, **changes)
        configuration = dict(self.configuration, baselines=[entry])
        return measurement.baseline_findings(self.root, configuration)

    def test_a_recorded_baseline_holds(self) -> None:
        self.assertEqual(self.findings(), [])

    def test_a_baseline_of_another_commit_is_refused(self) -> None:
        self.assertTrue(any('not a commit' in problem for problem in self.findings(commit='0' * 40)))

    def test_a_baseline_of_another_dataset_is_refused(self) -> None:
        (self.root / 'records/full-warm.json').write_text(json.dumps({'datasetIdentity': '0' * 64, 'pairs': 12}))
        self.assertTrue(any('another dataset' in problem for problem in self.findings()))

    def test_a_baseline_short_of_pairs_is_refused(self) -> None:
        identity = self.configuration['dataset']['identityDigest']
        (self.root / 'records/full-warm.json').write_text(json.dumps({'datasetIdentity': identity, 'pairs': 2}))
        self.assertTrue(any(f'fewer than {measurement.PROTOCOL["pairs"]} pairs' in problem for problem in self.findings()))

    def test_a_baseline_states_the_resolution_its_records_give(self) -> None:
        entry = dict(self.entry)
        entry.pop('resolution')
        configuration = dict(self.configuration, baselines=[entry])
        self.assertTrue(any('states no resolution' in problem
                            for problem in measurement.baseline_findings(self.root, configuration)))
        problems = self.findings(resolution={'wallTime': 1.05, 'residentPeak': 1.0002})
        self.assertTrue(any('wallTime' in problem and 'its records give 1.01' in problem for problem in problems),
                        problems)

    def test_a_missing_record_is_refused(self) -> None:
        self.assertTrue(any('does not exist' in problem for problem in self.findings(records=['records/absent.json'])))

    def test_a_baseline_of_a_commit_head_does_not_descend_from_is_refused(self) -> None:
        git(self.root, 'checkout', '-q', '-b', 'elsewhere')
        (self.root / 'README').write_text('y', encoding='utf-8')
        git(self.root, 'commit', '-q', '-am', 'elsewhere')
        elsewhere = git(self.root, 'rev-parse', 'HEAD')
        git(self.root, 'checkout', '-q', '-')
        manifest = json.loads((self.root / 'records/archive-manifest.json').read_text())
        (self.root / 'records/archive-manifest.json').write_text(json.dumps(dict(manifest, commit=elsewhere)))
        self.assertTrue(any('not an ancestor of HEAD' in problem for problem in self.findings(commit=elsewhere)))

    def test_a_baseline_names_a_known_path(self) -> None:
        self.assertEqual(self.findings(path='application'), [])
        self.assertTrue(any('neither driver nor application' in problem for problem in self.findings(path='window')))

    def test_each_path_is_decided_by_its_own_baselines_resolution(self) -> None:
        driver = dict(self.entry, resolution={'wallTime': 1.01, 'residentPeak': 1.0002})
        application = dict(self.entry, path='application', resolution={'wallTime': 1.04, 'residentPeak': 1.003})
        configuration = dict(self.configuration, baselines=[driver, application])
        self.assertEqual(measurement.host_resolution(configuration, 'bench-windows')['wallTime'], 1.01)
        self.assertEqual(measurement.host_resolution(configuration, 'bench-windows', 'application')['wallTime'], 1.04)
        alone = dict(self.configuration, baselines=[driver])
        resolution = measurement.host_resolution(alone, 'bench-windows', 'application')
        self.assertEqual(resolution['wallTime'], self.configuration['protocol']['noRegressionRatio'])
        self.assertIn('no recorded application baseline', resolution['source'])


def frame_set(roles: dict[str, list[str]]) -> dict:
    return {'id': 'set', 'roles': [{'role': role, 'frames': [{'relativePath': path} for path in paths]}
                                   for role, paths in roles.items()]}


class ApplicationPathTest(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = pathlib.Path(self.directory.name)
        self.corpus = self.root / 'corpus'
        self.roles = {'lights': ['night/L1.fits', 'night/L2.fits', 'night/other.fits'],
                      'bias': ['cal/B1.fits'], 'darks': ['cal/D1.fits'], 'flats': ['cal/F1.fits']}
        for paths in self.roles.values():
            for relative in paths:
                (self.corpus / relative).parent.mkdir(parents=True, exist_ok=True)
                (self.corpus / relative).write_bytes(relative.encode())
        self.roles['lights'].remove('night/other.fits')

    def test_each_role_is_staged_as_links_to_the_corpus_files_in_its_own_folder(self) -> None:
        frames = self.root / 'frames'
        counts = measurement.stage_frames(frame_set(self.roles), self.corpus, frames)
        self.assertEqual(counts, {'LIGHTS': 2, 'BIAS': 1, 'DARK': 1, 'FLAT': 1})
        self.assertTrue(os.path.samefile(frames / 'LIGHTS' / 'L1.fits', self.corpus / 'night/L1.fits'))
        self.assertFalse((frames / 'LIGHTS' / 'other.fits').exists(), 'a corpus file outside the set was staged')
        # Staging again keeps the links it made.
        self.assertEqual(measurement.stage_frames(frame_set(self.roles), self.corpus, frames), counts)

    def test_a_staged_folder_holding_anything_else_is_refused(self) -> None:
        frames = self.root / 'frames'
        measurement.stage_frames(frame_set(self.roles), self.corpus, frames)
        (frames / 'LIGHTS' / 'stray.fits').write_bytes(b'x')
        with self.assertRaisesRegex(measurement.MeasurementError, 'stray.fits'):
            measurement.stage_frames(frame_set(self.roles), self.corpus, frames)
        (frames / 'LIGHTS' / 'stray.fits').unlink()
        (frames / 'notes.txt').write_bytes(b'x')
        with self.assertRaisesRegex(measurement.MeasurementError, 'beside the role folders'):
            measurement.stage_frames(frame_set(self.roles), self.corpus, frames)

    def test_a_staged_frame_that_is_another_file_is_refused(self) -> None:
        frames = self.root / 'frames'
        (frames / 'BIAS').mkdir(parents=True)
        (frames / 'BIAS' / 'B1.fits').write_bytes(b'cal/B1.fits')
        with self.assertRaisesRegex(measurement.MeasurementError, 'is not'):
            measurement.stage_frames(frame_set(self.roles), self.corpus, frames)

    def test_a_role_no_window_adds_is_refused(self) -> None:
        with self.assertRaisesRegex(measurement.MeasurementError, 'no window adds'):
            measurement.stage_frames(frame_set({'masters': ['cal/B1.fits']}), self.corpus, self.root / 'frames')

    def command(self, platform_name: str) -> list[str]:
        with unittest.mock.patch.object(measurement, 'host_platform', return_value=platform_name), \
                unittest.mock.patch.dict(os.environ, {'SystemRoot': r'C:\Windows'}):
            return measurement.application_command(
                pathlib.Path('driver'), pathlib.Path('bin/AfterNight'), 'full', pathlib.Path('frames'),
                pathlib.Path('out'), pathlib.Path('storage'), pathlib.Path('result.json'), 1, 20000, 10, 'f' * 64)

    def test_the_command_drives_the_deployments_application_through_windows_powershell(self) -> None:
        self.assertEqual(self.command('windows'), [
            r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe', '-NoProfile', '-NonInteractive',
            '-ExecutionPolicy', 'Bypass', '-File', 'driver', '-Application', str(pathlib.Path('bin/AfterNight')),
            '-Workflow', 'full', '-Frames', 'frames', '-Output', 'out', '-Storage', 'storage', '-Result', 'result.json',
            '-Variant', '1', '-CancelAfterMs', '20000', '-SamplingIntervalMs', '10', '-DatasetIdentity', 'f' * 64])

    def test_the_command_drives_the_deployments_application_through_the_systems_python_on_linux(self) -> None:
        command = self.command('linux')
        # The project's Python, which runs this tool, carries numpy and not the AT-SPI client the driver needs.
        self.assertEqual(command[:2], ['/usr/bin/python3', 'driver'])
        arguments = dict(zip(command[2::2], command[3::2]))
        self.assertEqual(arguments, {
            '--application': str(pathlib.Path('bin/AfterNight')), '--workflow': 'full', '--frames': 'frames',
            '--output': 'out', '--storage': 'storage', '--result': 'result.json', '--variant': '1',
            '--cancel-after-ms': '20000', '--sampling-interval-ms': '10', '--dataset-identity': 'f' * 64})

    def test_each_platform_has_an_application_driver(self) -> None:
        for platform_name in ('windows', 'linux'):
            self.assertTrue((measurement.REPO_ROOT / measurement.APPLICATION_DRIVER[platform_name]).is_file(),
                            platform_name)

    def test_the_linux_driver_reads_the_command_it_is_given(self) -> None:
        parsed = linux_driver.parse_arguments(self.command('linux')[2:])
        self.assertEqual((parsed.application, parsed.workflow, parsed.frames, parsed.output, parsed.storage),
                         (str(pathlib.Path('bin/AfterNight')), 'full', 'frames', 'out', 'storage'))
        self.assertEqual((parsed.result, parsed.variant, parsed.cancel_after_ms, parsed.sampling_interval_ms),
                         ('result.json', 1, 20000, 10))
        self.assertEqual((parsed.dataset_identity, parsed.timeout_seconds), ('f' * 64, 1800))

    def test_a_deployment_runs_its_application_by_the_application_path(self) -> None:
        (self.root / 'bin').mkdir()
        application = self.root / 'bin' / measurement.APPLICATION[measurement.host_platform()]
        application.write_bytes(b'x')
        self.assertEqual(measurement.driver_path(self.root, 'application'), application)
        with self.assertRaisesRegex(measurement.MeasurementError, measurement.DRIVER):
            measurement.driver_path(self.root)

    def test_an_application_result_is_read_as_a_drivers_with_its_processor_time_in_the_region(self) -> None:
        summary = measurement.summarize_result({
            'outcome': {'success': True, 'completion': 'Complete'},
            'timing': {'totalMs': 70000.0, 'preparationMs': 50.0, 'executionMs': 69950.0,
                       'stages': [{'name': 'LightsStacking'}]},
            'memory': {'residentPeakBytes': 14 << 30, 'idleBaselineBytes': 1 << 28},
            'process': {'cpuSecondsInRegion': 1300.0, 'cpuSeconds': 1310.0},
            'preparation': {'executionMode': 'incremental'},
            'outputs': [{'role': 'light-master', 'path': 'stacked.fit', 'sha256': 'aa'}]})
        self.assertEqual((summary['totalMs'], summary['regionCpuSeconds'], summary['completion']),
                         (70000.0, 1300.0, 'Complete'))
        self.assertEqual(summary['stages'], ['LightsStacking'])
        self.assertEqual(summary['outputs'], {'light-master:stacked.fit': 'aa'})

    def test_the_campaign_estimate_adds_what_the_application_path_costs_per_run(self) -> None:
        configuration = measurement.read_configuration()
        driver = measurement.campaign_estimate(configuration, 'bench-windows')
        application = measurement.campaign_estimate(configuration, 'bench-windows', 'application')
        pairs = configuration['campaign']['hosts']['bench-windows']['pairs']
        extra = measurement.APPLICATION_RUN_OVERHEAD_SECONDS - measurement.RUN_OVERHEAD_SECONDS
        self.assertAlmostEqual(application['series']['full-warm'] - driver['series']['full-warm'], 2 * pairs * extra)
        self.assertEqual(application['path'], 'application')

    def test_a_campaign_over_its_budget_by_the_application_path_alone_is_refused(self) -> None:
        configuration = copy.deepcopy(measurement.read_configuration())
        seconds = configuration['hosts']['bench-windows']['runSeconds']
        # Fresh runs that leave the driver path just inside the budget leave the application path outside it.
        budget = configuration['campaign']['budgetMinutes'] * 60
        series = configuration['campaign']['hosts']['bench-windows']['series']
        driver = measurement.campaign_estimate(configuration, 'bench-windows')['totalSeconds']
        seconds['baseline']['fresh'] += (budget - driver - 1) / (2 * 3 * sum(1 for s in series if 'warm' in s))
        problems = measurement.campaign_findings(configuration)
        self.assertTrue(any('by the application path' in problem for problem in problems), problems)
        self.assertFalse(any('by the driver path' in problem for problem in problems), problems)

    def test_a_run_needs_numpy_before_its_first_pair(self) -> None:
        with unittest.mock.patch.object(measurement.importlib.util, 'find_spec', return_value=None):
            with self.assertRaisesRegex(measurement.MeasurementError, 'numpy'):
                measurement.require_numpy()

    def test_the_application_path_needs_a_frames_directory(self) -> None:
        output = self.root / 'out'
        with unittest.mock.patch('builtins.print') as printed:
            self.assertEqual(measurement.main(['run', '--baseline', str(self.root), '--candidate', str(self.root),
                                               '--workflow', 'full', '--case', 'warm', '--host-id', 'bench-windows',
                                               '--corpus-root', str(self.corpus), '--output', str(output),
                                               '--path', 'application']), 1)
        self.assertIn('--frames', ' '.join(str(call.args[0]) for call in printed.call_args_list))
        self.assertFalse(output.exists())

    def test_batches_of_two_paths_are_not_analysed_together(self) -> None:
        records = []
        for index, path in enumerate(('driver', 'application')):
            directory = self.root / f'batch{index}'
            directory.mkdir()
            record = runs_record([run(1, 'baseline', 0, 100.0, 1), run(2, 'candidate', 0, 100.0, 1)])
            record['path'] = path
            (directory / 'runs.json').write_text(json.dumps(record), encoding='utf-8')
            records.append(directory)
        with self.assertRaisesRegex(measurement.MeasurementError, 'differ in path'):
            measurement.compare(records)



class LinuxApplicationDriverTest(unittest.TestCase):
    """What the Linux application driver reads outside the application: the run's log, the kernel's account of the
    process and the objects it mapped. The windows themselves are driven only against a real application."""

    RULE = '\u2550\u2550\u2550'
    LOG = ['[2026-10-18T23:12:12.274] [HEADER] WORKFLOW STARTED',
           f'{RULE} STAGE: Bias Stacking (1 group) {RULE}',
           f'[2026-10-18T23:12:33.580] [SUCCESS] {RULE} Bias Stacking Complete {RULE} (1 group, 21 s)',
           f'{RULE} STAGE: Lights Stacking (2 groups) {RULE}',
           f'[2026-10-18T23:13:00.080] [SUCCESS] {RULE} Lights Stacking Complete {RULE} (2 groups, 26 s)',
           '[2026-10-18T23:13:00.773] [DETAIL]     \u2192 Engine memory: peak 14226310712 bytes (0 when the run '
           'started), 5 bytes still allocated, 70727 allocations of 278610723184 bytes, 2 refused']

    def test_each_stage_is_timed_from_the_log_and_the_engine_line_is_read(self) -> None:
        stages, engine = linux_driver.read_workflow_log(self.LOG)
        self.assertEqual([stage['name'] for stage in stages], ['BiasStacking', 'LightsStacking'])
        self.assertAlmostEqual(stages[0]['ms'], 21306.0)
        self.assertAlmostEqual(stages[1]['ms'], 26500.0)
        self.assertAlmostEqual(stages[1]['endMs'], 47806.0)
        self.assertEqual(engine, {'region': {'peakBytes': 14226310712, 'peakBeforeRegionBytes': 0,
                                             'liveBytesAfterRegion': 5, 'allocationsInRegion': 70727,
                                             'bytesAllocatedInRegion': 278610723184, 'deniedAllocationsInRegion': 2}})

    def test_a_log_without_the_engine_line_has_stages_and_no_engine_accounting(self) -> None:
        stages, engine = linux_driver.read_workflow_log(self.LOG[:5])
        self.assertEqual(len(stages), 2)
        self.assertIsNone(engine)

    def test_a_stage_that_did_not_complete_is_named_without_a_time(self) -> None:
        stages, _ = linux_driver.read_workflow_log(self.LOG[:4])
        self.assertEqual(stages[1], {'name': 'LightsStacking'})

    def test_the_memory_lines_of_the_process_status_are_read_in_bytes(self) -> None:
        fields = linux_driver._status_fields('Name:\tAfterNight\nVmHWM:\t  2048 kB\nVmRSS:\t  1024 kB\n'
                                             'RssAnon:\t   512 kB\nThreads:\t9\n')
        self.assertEqual(fields, {'VmHWM': 2 << 20, 'VmRSS': 1 << 20, 'RssAnon': 512 << 10})

    def test_a_mapped_library_outside_the_deployment_and_the_system_is_reported(self) -> None:
        deployment = pathlib.Path('/archive/bin')
        checkout = pathlib.Path('/home/someone/afternight')
        sorted_libraries = linux_driver.classify_shared_objects(
            {'/archive/bin/libAfterNightControls.so', '/archive/bin/lib/libQt6Core.so.6.10.3',
             '/usr/lib/x86_64-linux-gnu/libc.so.6', '/opt/other/libfoo.so.1',
             '/home/someone/afternight/Qt6/6.10.3/gcc_64/lib/libQt6Gui.so.6.10.3'}, deployment, checkout)
        self.assertEqual(len(sorted_libraries['deployment']), 2)
        self.assertEqual(sorted_libraries['system'], ['/usr/lib/x86_64-linux-gnu/libc.so.6'])
        qt = '/home/someone/afternight/Qt6/6.10.3/gcc_64/lib/libQt6Gui.so.6.10.3'
        self.assertEqual(sorted_libraries['outside'], [qt, '/opt/other/libfoo.so.1'])
        self.assertEqual(sorted_libraries['checkout'], [qt])

    def test_a_deployment_inside_the_checkout_is_the_deployment(self) -> None:
        sorted_libraries = linux_driver.classify_shared_objects(
            {'/home/someone/afternight/bin/clang/release/libAfterNightControls.so'},
            pathlib.Path('/home/someone/afternight/bin/clang/release'), pathlib.Path('/home/someone/afternight'))
        self.assertEqual((len(sorted_libraries['deployment']), sorted_libraries['checkout']), (1, []))

    def test_a_run_refuses_a_library_of_the_checkout(self) -> None:
        driver = linux_driver.ApplicationDriver(linux_driver.parse_arguments(
            ['--application', 'a', '--workflow', 'full', '--frames', 'f', '--output', 'o', '--storage', 's',
             '--result', 'r']))
        record = {'process': {}}
        library = str(linux_driver.REPOSITORY / 'Qt6' / 'lib' / 'libQt6Core.so.6')
        with self.assertRaisesRegex(linux_driver.DriverError, 'libraries of the checkout'):
            driver.refuse_checkout_libraries(record, {library}, pathlib.Path('/archive/bin'))
        self.assertEqual(record['process']['sharedObjects']['outside'], [library])

    def test_only_the_images_the_run_published_after_its_request_are_its_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / 'out'
            (output / 'history' / 'run').mkdir(parents=True)
            (output / 'history' / 'run' / 'workflow_log.txt').write_text('\n'.join(self.LOG), encoding='utf-8')
            for name in ('master_bias_1x1.fit', 'stacked_1x1_Red.fit', 'registered_1.fit', 'old_master_dark.fit'):
                (output / name).write_bytes(name.encode())
            earlier = os.stat(output / 'master_bias_1x1.fit').st_mtime - 60
            os.utime(output / 'old_master_dark.fit', (earlier, earlier))
            storage = pathlib.Path(directory) / 'storage'
            (storage / 'logs').mkdir(parents=True)
            (storage / 'logs' / 'AfterNight.log').write_text('WorkflowExecutor: starting incremental execution\n',
                                                             encoding='utf-8')
            record = linux_driver.new_record(linux_driver.parse_arguments(
                ['--application', 'a', '--workflow', 'full', '--frames', 'f', '--output', str(output), '--storage',
                 str(storage), '--result', 'r']))
            linux_driver.collect_run_outputs(record, output, storage, earlier + 30)
        self.assertEqual([(entry['role'], entry['path']) for entry in record['outputs']],
                         [('bias-master', 'master_bias_1x1.fit'), ('light-master', 'stacked_1x1_Red.fit')])
        self.assertEqual(record['outputs'][0]['sha256'], hashlib.sha256(b'master_bias_1x1.fit').hexdigest())
        self.assertEqual(record['preparation']['executionMode'], 'incremental')
        self.assertEqual(record['workflowLog'], 'history/run/workflow_log.txt')
        self.assertEqual(record['engine']['region']['deniedAllocationsInRegion'], 2)
        summary = measurement.summarize_result(record)
        self.assertEqual(summary['stages'], ['BiasStacking', 'LightsStacking'])
        self.assertEqual(summary['outputs'], {'bias-master:master_bias_1x1.fit': record['outputs'][0]['sha256'],
                                              'light-master:stacked_1x1_Red.fit': record['outputs'][1]['sha256']})


if __name__ == '__main__':
    unittest.main()
