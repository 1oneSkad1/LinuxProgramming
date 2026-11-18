"""Independent fixture expectations, error handling, and parallel equivalence."""
import csv
import ipaddress
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

BASE = Path(__file__).resolve().parents[1]
BINARY = (BASE / os.environ.get('TRAFFIC_BINARY', 'traffic_analyzer')).resolve()

def packet(src='10.0.0.1', dst='10.0.0.2', size=100, sec=1785596400, usec=0,
           version=4, ihl=5, total=40, fragment=0):
    payload = bytearray(40)
    payload[0] = version << 4 | ihl
    struct.pack_into('!H', payload, 2, total)
    struct.pack_into('!H', payload, 6, fragment)
    payload[9] = 6
    payload[12:16] = ipaddress.IPv4Address(src).packed
    payload[16:20] = ipaddress.IPv4Address(dst).packed
    return struct.pack('<IIHH', sec, usec, size, len(payload)) + payload

def rows(out, name):
    with (out / name).open() as f:
        return list(csv.DictReader(f))

class AnalyzerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='traffic-tests-', dir=BASE)
        self.root = Path(self.tmp.name)
        self.data = self.root / 'input'; self.data.mkdir()
        self.counter = 0
    def tearDown(self):
        self.tmp.cleanup()
    def write(self, data, name='pkt_2026.08.02_00.00'):
        p = self.data / name; p.write_bytes(data); return p
    def run_analyzer(self, *extra, expected=0, source=None):
        self.counter += 1; out = self.root / f'out{self.counter}'
        result = subprocess.run([str(BINARY), '--input', str(source or self.data),
            '--output', str(out), *extra], capture_output=True, text=True)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return out

    def test_hand_calculated_and_parallel_equivalence(self):
        self.write(packet(size=100) + packet('10.0.0.2', '10.0.0.1', 200))
        self.write(packet('10.0.0.1', '10.0.0.1', 50, sec=1785596460), 'pkt_2026.08.02_00.01')
        ref = self.run_analyzer('--mode', 'sequential', '--buffer-size', '17')
        summary = rows(ref, 'summary.csv')[0]
        self.assertEqual((summary['packets'], summary['bytes'], summary['unique_hosts']), ('3','350','2'))
        hosts = {r['ip']: r for r in rows(ref, 'hosts.csv')}
        self.assertEqual([hosts['10.0.0.1'][k] for k in ('tx_packets','tx_bytes','rx_packets','rx_bytes')], ['2','150','2','250'])
        self.assertEqual([r['bytes'] for r in rows(ref, 'timeseries.csv')], ['300','50'])
        self.assertEqual(rows(ref, 'timeseries.csv')[0]['local_time'], '2026-08-02T00:00:00')
        for workers in (1,2,4,8):
            out = self.run_analyzer('--workers', str(workers), '--buffer-size', '31')
            for name in ('summary.csv','hosts.csv','timeseries.csv','top_hosts.csv','errors.log'):
                self.assertEqual((ref/name).read_bytes(), (out/name).read_bytes(), name)

    def test_truncated_header_and_payload_keep_valid_prefix(self):
        for bad in (b'abc', packet()[:-1]):
            p = self.write(packet()+bad)
            out = self.run_analyzer(source=p, expected=1)
            s = rows(out,'summary.csv')[0]
            self.assertEqual((s['packets'],s['status'],s['failed_files']),('1','partial','1'))
            self.assertIn('truncated', (out/'errors.log').read_text())

    def test_bad_record_then_good_record(self):
        self.write(packet(usec=1000000)+packet(ihl=4)+packet(total=19)+packet(size=30)+packet())
        out=self.run_analyzer(expected=1)
        s=rows(out,'summary.csv')[0]
        self.assertEqual((s['packets'],s['invalid_records']),('1','4'))

    def test_empty_file(self):
        self.write(b'')
        out=self.run_analyzer()
        self.assertEqual(rows(out,'summary.csv')[0]['packets'],'0')
        self.assertEqual(rows(out,'hosts.csv'),[])

    def test_empty_directory_and_missing_path(self):
        self.run_analyzer(expected=2)
        self.run_analyzer(expected=2, source=self.root/'missing')

    def test_skipped_records_and_fragments(self):
        self.write(struct.pack('<IIHH',1785596400,0,100,0)+packet(version=6)+packet(fragment=3))
        out=self.run_analyzer()
        s=rows(out,'summary.csv')[0]
        self.assertEqual((s['packets'],s['skipped_records']),('1','2'))

    def test_many_hosts_rehash_and_repeatable_ranking(self):
        count=3000
        self.write(b''.join(packet(str(ipaddress.IPv4Address(0x0a000000+i)), '192.0.2.1', 100) for i in range(count)))
        a=self.run_analyzer('--mode','sequential');b=self.run_analyzer('--workers','8')
        h=rows(a,'hosts.csv')
        self.assertEqual(len(h),count+1)
        self.assertEqual(sum(int(x['tx_bytes']) for x in h), count*100)
        self.assertEqual(sum(int(x['rx_bytes']) for x in h), count*100)
        self.assertEqual(rows(a,'top_hosts.csv')[0]['ip'],'192.0.2.1')
        self.assertEqual((a/'hosts.csv').read_bytes(),(b/'hosts.csv').read_bytes())

    def test_dates_do_not_merge(self):
        self.write(packet()+packet(sec=1785682800))
        out=self.run_analyzer()
        self.assertEqual([r['local_time'] for r in rows(out,'timeseries.csv')],['2026-08-02T00:00:00','2026-08-03T00:00:00'])

    def test_directory_filter_and_symlink(self):
        p=self.write(packet())
        self.write(b'not packets','pkt_2026.08.02_00.01.txt')
        (self.data/'pkt_2026.08.02_00.02').symlink_to(p)
        out=self.run_analyzer()
        self.assertEqual(rows(out,'summary.csv')[0]['files'],'1')

    def test_output_nested_and_no_overwrite(self):
        self.write(packet())
        out=self.data/'reports'/'run1'
        cmd=[str(BINARY),'--input',str(self.data),'--output',str(out)]
        self.assertEqual(subprocess.run(cmd,capture_output=True).returncode,0)
        before=(out/'summary.csv').read_bytes()
        self.assertEqual(subprocess.run(cmd,capture_output=True).returncode,2)
        self.assertEqual((out/'summary.csv').read_bytes(),before)

    def test_invalid_options(self):
        self.write(packet())
        for key,value in (('--workers','0'),('--workers','129'),('--interval','0'),('--buffer-size','15'),('--top','-1'),('--timezone-minutes','9999'),('--mode','wrong')):
            self.run_analyzer(key,value,expected=2)

    def test_output_path_is_file(self):
        self.write(packet())
        out=self.root/'occupied';out.write_text('keep')
        result=subprocess.run([str(BINARY),'--input',str(self.data),'--output',str(out)],capture_output=True)
        self.assertEqual(result.returncode,2);self.assertEqual(out.read_text(),'keep')

    def test_progress_and_quiet_preserve_results(self):
        self.write(packet())
        self.write(packet(), 'pkt_2026.08.02_00.01')
        for mode in ('sequential', 'threaded'):
            outputs = []
            for quiet in (False, True):
                out = self.root / f'progress_{mode}_{quiet}'
                cmd = [str(BINARY), '--input', str(self.data), '--output', str(out),
                       '--mode', mode, '--workers', '4']
                if quiet:
                    cmd.append('--quiet')
                result = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stderr)
                if quiet:
                    self.assertEqual(result.stderr, '')
                else:
                    self.assertIn('[progress] files 0/2 (0.0%)', result.stderr)
                    self.assertIn('[progress] files 2/2 (100.0%)', result.stderr)
                    self.assertIn('failed 0', result.stderr)
                    self.assertIn('Merging statistics...', result.stderr)
                    self.assertIn('Saving CSV reports...', result.stderr)
                    self.assertNotIn('\x1b', result.stderr)
                outputs.append(out)
            for name in ('summary.csv', 'hosts.csv', 'timeseries.csv', 'top_hosts.csv', 'errors.log'):
                self.assertEqual((outputs[0] / name).read_bytes(), (outputs[1] / name).read_bytes())

    def test_progress_reports_failed_files(self):
        self.write(packet())
        self.write(packet()[:-1], 'pkt_2026.08.02_00.01')
        out = self.root / 'partial_progress'
        result = subprocess.run([str(BINARY), '--input', str(self.data), '--output', str(out),
                                 '--workers', '4'], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertRegex(result.stderr, r'files 2/2 \(100\.0%\).*failed 1')
        self.assertIn('status=partial', result.stdout)

if __name__=='__main__':
    unittest.main(verbosity=2)
