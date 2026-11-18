"""Generate a Korean implementation/experiment report from recorded results."""
import csv
import json
from pathlib import Path

BASE=Path(__file__).resolve().parents[1]

def read_row(path):
    with path.open(encoding='utf-8') as f:return next(csv.DictReader(f))

def main():
    full=BASE/'results/full_day';bench=BASE/'results/benchmark'
    validation=json.loads((full/'validation.json').read_text())
    env=json.loads((bench/'environment.json').read_text())
    with (bench/'comparison.csv').open() as f:comparison=list(csv.DictReader(f))
    seq=read_row(full/'sequential/run_metrics.csv');par=read_row(full/'threaded/run_metrics.csv')
    s=validation['summary']
    lines=['# 구현 및 검증 보고서','',
        '## 구현 결과','',
        'C11과 POSIX API로 동일 형식의 트래픽 데이터를 반복 분석하는 프로그램을 구현했습니다. `open/read/close` 기반 버퍼 읽기, 파일 단위 작업 큐, pthread 워커별 해시 집계, 최종 병합 및 CSV 출력을 지원합니다. 출력은 새로운 디렉터리에 생성하며 기존 결과를 덮어쓰지 않습니다.','',
        '## 입력 형식','',
        '12바이트 little-endian 저장 헤더 뒤에 저장 길이만큼의 IPv4 데이터를 읽습니다. 저장 헤더의 원본 `pkt_len`을 바이트 통계에 사용합니다. 길이·타임스탬프·IPv4 헤더를 검증하며 파일 중간의 잘림은 부분 결과와 오류 종료 상태로 보고합니다. 통계 시간 구간은 Unix epoch 기준으로 계산하고 기본 출력 시간대는 UTC+09:00입니다.','',
        '## 소규모 검증','',
        '예상 값을 직접 계산한 검증 데이터와 12개 자동 테스트를 사용했습니다. 순차·1·2·4·8 스레드 결과 일치, 버퍼 경계, 손상·잘린 레코드, 빈 입력, 해시 확장, 날짜 구분, 자기 자신에게 송수신, 파일 필터, 출력 오류와 덮어쓰기 방지를 확인했습니다. AddressSanitizer 및 UndefinedBehaviorSanitizer에서도 같은 12개 테스트를 통과했습니다.','',
        '## 하루치 전체 데이터 검증','',
        '| 항목 | 결과 |','|---|---:|',
        f"| 파일 수 | {int(s['files']):,} |",
        f"| 유효 IPv4 패킷 수 | {int(s['packets']):,} |",
        f"| 원본 패킷 바이트 합 | {int(s['bytes']):,} |",
        f"| 고유 호스트 수 | {int(s['unique_hosts']):,} |",
        f"| 실패 파일 수 | {s['failed_files']} |",
        f"| 제외된 레코드 수 | {s['skipped_records']} |",
        f"| 완전히 읽었으나 필드가 잘못된 레코드 수 | {s['invalid_records']} |",'',
        '헤더·본문이 잘려 완전히 읽지 못한 레코드는 위의 필드 오류 레코드 수에 포함되지 않으며, 실패 파일 수와 errors.log로 보고합니다.','',
        '순차 실행과 4개 스레드 실행의 summary, hosts, timeseries, top_hosts 및 errors.log가 바이트 단위로 일치했습니다. 호스트 전체 송신 바이트 합과 수신 바이트 합은 각각 전체 바이트 통계와 일치했습니다. 00:00·12:00·23:59 파일은 별도로 작성한 Python 파서의 IP별·시간대별 통계와도 일치했습니다.','',
        '**원본 데이터 문제:** `pkt_2026.08.02_19.27`은 3,145,728바이트이며 마지막 레코드의 저장 헤더는 데이터 28바이트를 요구하지만 실제로는 25바이트만 남습니다. 서버 원본 전체와 로컬 파일의 SHA-256이 `32279941af38767b6bb0a0f598a5d31a11e09799f3f9e3ca9c0a2bfd4f88c903`으로 일치하여 다운로드 손상이 아닌 제공 원본의 잘림임을 확인했습니다. 이 파일에서 정상적으로 읽은 앞부분은 포함하고 마지막 불완전한 레코드는 제외합니다. 전체 결과는 `partial`, 종료 코드는 1이며 원본 수정이나 추정 복구는 수행하지 않았습니다. 근거는 `results/full_day/source_issue.json`과 `errors.log`에 있습니다.','',
        f"전체 입력 실행 시간은 순차 {float(seq['elapsed_seconds']):.3f}초, 4개 스레드 {float(par['elapsed_seconds']):.3f}초였습니다. 각 1회 실행한 정확성 검증 기록이므로 반복 성능 실험의 결론으로 사용하지 않습니다.",'',
        '## 반복 성능 실험','',
        f"하루 전체에서 고르게 선택한 {env['files']}개 파일, {env['input_bytes']:,}바이트를 대상으로 조건별 {env['repeats']}회 실행했습니다. 순차 예열 후 실행 순서를 고정 난수로 섞었으며 모든 실행의 분석 CSV가 기준 결과와 일치했습니다.",'',
        '| 실행 방식 | 스레드 수 | 중앙값(초) | 최소~최대(초) | 순차 대비 배속 | 최대 RSS(MiB) |',
        '|---|---:|---:|---:|---:|---:|']
    for r in comparison:
        lines.append(f"| {r['mode']} | {r['workers']} | {float(r['median_seconds']):.4f} | {float(r['min_seconds']):.4f}~{float(r['max_seconds']):.4f} | {float(r['speedup']):.2f} | {int(r['max_rss_kib'])/1024:.2f} |")
    best=min(comparison,key=lambda r:float(r['median_seconds']))
    lines+=['','![실행 시간 중앙값 비교](results/benchmark/comparison.png)','',f"이 표본에서 가장 짧은 중앙값은 {best['mode']} {best['workers']}개 스레드 조건입니다. 이 결과는 해당 장비·캐시·Windows 마운트 경로에서의 표본 실험이며 다른 저장장치나 하루 전체의 최적 스레드 수로 일반화하지 않습니다. 일부 순차·1개 스레드 실행의 변동 폭이 크고 4·8개 스레드의 범위도 겹치므로, 3회 실험의 중앙값을 탐색적 결과로 제시합니다.",'',
        '## 측정 환경과 한계','',
        f"- 운영체제: {env['platform']}",f"- 논리 CPU 수: {env['cpu_count']}",
        f"- 컴파일러: {env['compiler']}",f"- 컴파일 옵션: `{env['compile_flags']}`",'',
        '원본과 성능 표본 모두 WSL의 `/mnt/c` 경로에서 읽었습니다. 표본은 복사 과정과 예열로 캐시에 올라갈 수 있으며 캐시를 강제로 비우지 않았습니다. 표본 처리 속도와 최초 하루치 순차 실행 시간을 직접 비교하면 안 됩니다. `elapsed_seconds`에는 파일 탐색·집계·병합·주요 결과 저장이 포함되며 마지막 성능 메타데이터 파일 기록 시간은 제외됩니다. 반복 실험의 RSS는 Linux `/proc/self/status`의 실행 이미지별 VmHWM 값이며 런처의 최고점이 섞일 수 있는 getrusage 값도 원자료에 별도 기록했습니다.','',
        '파일 단위 분배이므로 단일 파일에 워커를 여러 개 붙이지 않습니다. 워커별 해시 테이블로 잠금 경쟁을 줄이는 대신 고유 IP 수와 워커 수에 따라 메모리 사용량이 늘어납니다. IPv6·포트별 분석·과금·급증 탐지는 현재 범위에 포함하지 않습니다.','',
        '## 산출물','',
        '- 실행 코드: `src/`, `include/`, `Makefile`',
        '- 사용 방법과 입력·통계 정의: `README.md`',
        '- 하루치 결과와 검증: `results/full_day/`',
        '- 반복 실험 원자료와 비교표: `results/benchmark/`',
        '- 재현 스크립트: `scripts/validate_real.py`, `scripts/benchmark.py`','']
    (BASE/'구현_및_검증_보고서.md').write_text('\n'.join(lines),encoding='utf-8')
    print(BASE/'구현_및_검증_보고서.md')

if __name__=='__main__':main()
