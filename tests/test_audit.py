import unittest
from tests.audit_live import Stats


class AuditEndStateTests(unittest.TestCase):
    def test_no_messages_is_not_live(self):
        report = Stats().report()
        self.assertFalse(report['stream_present_at_end'])
        self.assertIsNone(report['tail_silence_ms'])

    def test_stopped_stream_cannot_hide_behind_earlier_good_intervals(self):
        stats = Stats()
        stats.times = [10.0, 10.2, 10.4]
        stats.ended_mono = 15.0
        report = stats.report()
        self.assertAlmostEqual(report['avg_hz'], 5.0)
        self.assertAlmostEqual(report['tail_silence_ms'], 4600)
        self.assertFalse(report['stream_present_at_end'])

    def test_recent_message_is_live(self):
        stats = Stats()
        stats.times = [10.0, 10.1]
        stats.ended_mono = 10.15
        self.assertTrue(stats.report()['stream_present_at_end'])


if __name__ == '__main__':
    unittest.main()
