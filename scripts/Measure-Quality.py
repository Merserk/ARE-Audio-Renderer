"""Detailed, image-free DirectShow/WASAPI audio measurements.
MIT License. Copyright (c) 2026 Merserk.
Requires NumPy, SciPy and SoundFile. Live capture is opt-in and audible.
"""
from __future__ import annotations
import argparse
import csv
from datetime import date
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import time
import wave

import numpy as np
import soundfile as sf
from scipy import signal, optimize, fft
import scipy

ROOT = Path(__file__).resolve().parents[1]
MODES = ('system', 'r8brain', 'sinc')
LABELS = ('System Default', 'ARE / r8brain', 'ARE / SoX Sinc')
BODY_START = 1.45
FREQUENCIES = np.unique(np.rint(np.geomspace(80, 20000, 31)).astype(int))


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def db(value):
    return float(20 * np.log10(max(float(value), 1e-300)))


def write_pcm(path, x, rate, bits):
    q = np.clip(np.rint(x * 2 ** (bits - 1)), -2 ** (bits - 1), 2 ** (bits - 1) - 1).astype(np.int32)
    if bits == 16:
        packed = q.astype('<i2').tobytes()
    else:
        flat = q.ravel()
        packed = np.column_stack((flat & 255, (flat >> 8) & 255, (flat >> 16) & 255)).astype(np.uint8).tobytes()
    with wave.open(str(path), 'wb') as w:
        w.setparams((2, bits // 8, rate, len(q), 'NONE', 'not compressed'))
        w.writeframes(packed)
    return q.astype(np.float64) / 2 ** (bits - 1)


def ideal_rate(x, source_rate, destination_rate):
    if source_rate == destination_rate:
        return x.copy()
    # Fourier reference with one second of zero padding on both ends.
    # Used as a digital comparison reference, not an analog ground truth.
    padded = np.pad(x, ((source_rate, source_rate), (0, 0)))
    count = round(len(padded) * destination_rate / source_rate)
    return signal.resample(padded, count, axis=0)[destination_rate:destination_rate + round(len(x) * destination_rate / source_rate)]


def fixtures(args):
    inputs = args.work_dir / 'inputs'
    inputs.mkdir(parents=True, exist_ok=True)
    info = sf.info(args.music)
    with sf.SoundFile(args.music) as f:
        f.seek(round(args.music_start * info.samplerate))
        music = f.read(round(12 * info.samplerate), dtype='float64', always_2d=True)
    if len(music) != 12 * info.samplerate or music.shape[1] != 2:
        raise ValueError('Music must provide twelve seconds of stereo audio at the requested offset')
    peak = np.max(np.abs(music))
    if peak == 0:
        raise ValueError('Music excerpt is silent')
    music = music * (10 ** (-18 / 20) / peak)
    cases = []
    for kind, rates in (('music', (44100, 48000, 96000)), ('tone', (44100, 48000, 96000)),
                        ('multitone', (44100, 48000, 96000)), ('lowlevel', (96000,)),
                        ('ultrasonic', (96000,)), ('stereo', (48000,)), ('silence', (48000,))):
        for rate in rates:
            duration = 12 if kind == 'music' else 4
            t = np.arange(rate * duration) / rate
            amplitude = 10 ** (-18 / 20)
            if kind == 'music':
                body = ideal_rate(music, info.samplerate, rate)
            elif kind in ('tone', 'lowlevel'):
                a = 10 ** ((-90 if kind == 'lowlevel' else -18) / 20)
                body = np.repeat((a * np.sin(2 * np.pi * 1000 * t))[:, None], 2, axis=1)
            elif kind == 'multitone':
                phases = -np.pi * np.arange(len(FREQUENCIES)) ** 2 / len(FREQUENCIES)
                mono = sum(np.sin(2 * np.pi * f * t + phase) for f, phase in zip(FREQUENCIES, phases))
                body = np.repeat((mono * amplitude / np.max(np.abs(mono)))[:, None], 2, axis=1)
            elif kind == 'ultrasonic':
                mono = 10 ** (-24 / 20) * (np.sin(2 * np.pi * 1000 * t) + np.sin(2 * np.pi * 29000 * t))
                body = np.repeat(mono[:, None], 2, axis=1)
            elif kind == 'stereo':
                body = amplitude * np.column_stack((np.sin(2 * np.pi * 1000 * t), np.sin(2 * np.pi * 1700 * t)))
            else:
                body = np.zeros((len(t), 2))
            fade = np.linspace(0, 1, round(.02 * rate))
            body[:len(fade)] *= fade[:, None]
            body[-len(fade):] *= fade[::-1, None]
            mt = np.arange(round(.3 * rate)) / rate
            marker = signal.chirp(mt, 200, .3, 8000) * np.sin(np.pi * mt / .3) ** 2 * amplitude
            ct = np.arange(round(.75 * rate)) / rate
            calibration = amplitude * np.sin(2 * np.pi * 1000 * ct)
            calibration[:len(fade)] *= fade
            calibration[-len(fade):] *= fade[::-1]
            stereo = lambda v: np.repeat(v[:, None], 2, axis=1)
            x = np.concatenate((stereo(marker), np.zeros((round(.2 * rate), 2)), stereo(calibration),
                                np.zeros((round(.2 * rate), 2)), body, np.zeros((round(.2 * rate), 2))))
            bits = 16 if kind == 'music' and rate == 44100 else 24
            name = f'{kind}_{rate}'
            path = inputs / (name + '.wav')
            decoded = write_pcm(path, x, rate, bits)
            reference = ideal_rate(decoded, rate, 48000)
            cases.append({'name': name, 'kind': kind, 'rate': rate, 'bits': bits, 'duration': duration,
                          'wav': path, 'input_frames': len(decoded), 'input_sha256': digest(path),
                          'reference': reference})
    source = {'title': args.music_title or args.music.stem, 'sha256': digest(args.music),
              'native_rate': info.samplerate, 'native_subtype': info.subtype,
              'native_duration_seconds': info.duration,
              'excerpt_start_seconds': args.music_start, 'excerpt_duration_seconds': 12,
              'normalization': 'Excerpt peak scaled to -18 dBFS before fixture quantization',
              'derived_formats': '48/96 kHz fixtures are converted from the same recording, not native high-resolution masters'}
    return cases, source


def fitted_tone(x, rate, frequency):
    t = np.arange(len(x)) / rate
    basis = np.column_stack((np.sin(2 * np.pi * frequency * t), np.cos(2 * np.pi * frequency * t), np.ones(len(x))))
    coefficients = np.linalg.lstsq(basis, x, rcond=None)[0]
    fit = basis[:, :2] @ coefficients[:2]
    residual = x - basis @ coefficients
    return float(np.sqrt(np.mean(fit ** 2))), float(np.sqrt(np.mean(residual ** 2)))


def analysis(case, prefix):
    metadata = json.loads(prefix.with_suffix('.json').read_text())
    if metadata.get('preroll_peak', 0) != 0:
        raise ValueError('Nonzero audio before the test started; quiet baseline validation failed')
    rate = metadata['rate']
    if rate != 48000 or metadata['channels'] != 2:
        raise ValueError('This experiment requires a stereo 48 kHz loopback endpoint')
    capture = np.fromfile(prefix.with_suffix('.f32'), dtype='<f4').reshape(-1, 2).astype(np.float64)
    if len(capture) != metadata['frames']:
        raise ValueError('Raw capture frame count does not match its metadata')
    if not np.isfinite(capture).all():
        raise ValueError('Nonfinite loopback samples')
    reference = case['reference']
    marker = reference[:round(.3 * rate), 0]
    corr = signal.correlate(capture[:, 0], marker, mode='valid', method='fft')
    origin = int(np.argmax(np.abs(corr)))
    first, last = round((BODY_START + .5) * rate), round((BODY_START + case['duration'] - .5) * rate)
    if origin + last > len(capture):
        raise ValueError('Truncated capture')
    y = capture[origin + first:origin + last]
    # Use one shared stereo gain and a single fractional delay for waveform comparison.
    # Neither channel is independently equalized or dynamically warped.
    nfft = fft.next_fast_len(len(reference) + 2 * rate)
    padded = np.pad(reference, ((rate, nfft - rate - len(reference)), (0, 0)))
    spectrum = fft.rfft(padded, axis=0)
    frequency = fft.rfftfreq(nfft)
    def shifted(delta):
        return fft.irfft(spectrum * np.exp(-2j * np.pi * frequency[:, None] * delta), nfft, axis=0)[rate + first:rate + last]
    def objective(delta):
        ref = shifted(delta)[::8]
        target = y[::8]
        denominator = np.sum(ref ** 2)
        if denominator == 0:
            return 0.0
        gain = np.sum(ref * target) / denominator
        return float(np.mean((target - gain * ref) ** 2))
    delta = float(optimize.minimize_scalar(objective, bounds=(-1.1, 1.1), method='bounded',
                                           options={'xatol': 1e-7, 'maxiter': 35}).x) if case['kind'] != 'silence' else 0.0
    ref = reference[first:last] if case['kind'] == 'silence' else shifted(delta)
    denominator = float(np.sum(ref ** 2))
    gain = float(np.sum(ref * y) / denominator) if denominator else 0.0
    residual = y - gain * ref
    a, b = round(.65 * rate), round(1.1 * rate)
    calibration, _ = fitted_tone(capture[origin + a:origin + b, 0], rate, 1000)
    expected_calibration, _ = fitted_tone(reference[a:b, 0], rate, 1000)
    calibration_gain = calibration / expected_calibration
    peak, rms = float(np.max(np.abs(y))), float(np.sqrt(np.mean(y ** 2)))
    metrics = {'fractional_alignment_samples': delta, 'calibration_gain_db': db(calibration_gain),
               'captured_peak': peak, 'captured_rms': rms,
               'captured_peak_dbfs': db(peak) if peak else None, 'captured_rms_dbfs': db(rms) if rms else None,
               'clipped_samples': int(np.count_nonzero(np.abs(y) >= 1.0)),
               'analysis_frames': len(y), 'alignment_frame': origin,
               'sample_exact_at_integer_alignment': bool(np.array_equal(y, reference[first:last])),
               'waveform_snr_db': db(np.sqrt(np.mean((gain * ref) ** 2)) / max(np.sqrt(np.mean(residual ** 2)), 1e-30)) if denominator else None}
    if case['kind'] == 'music':
        segment_snr = []
        for start in range(0, len(y), rate):
            desired, error = gain * ref[start:start + rate], residual[start:start + rate]
            segment_snr.append(db(np.sqrt(np.mean(desired ** 2)) / max(np.sqrt(np.mean(error ** 2)), 1e-30)))
        metrics['one_second_waveform_snr_db'] = segment_snr
    if case['kind'] in ('tone', 'lowlevel', 'ultrasonic'):
        fundamental, noise = fitted_tone(y[:, 0], rate, 1000)
        metrics['thdn_dbc'] = db(noise / max(fundamental, 1e-30))
        f, power = signal.periodogram(y[:, 0], rate, window='hann', scaling='spectrum')
        metrics['residual_19khz_dbc'] = float(10 * np.log10(max(power[np.argmin(abs(f - 19000))], 1e-300) / max(power[np.argmin(abs(f - 1000))], 1e-300)))
        metrics['residual_15_1khz_dbc'] = float(10 * np.log10(max(power[np.argmin(abs(f - 15100))], 1e-300) / max(power[np.argmin(abs(f - 1000))], 1e-300)))
        metrics['reference_level_error_db'] = db(fundamental / max(calibration_gain * fitted_tone(ref[:, 0], rate, 1000)[0], 1e-30))
    if case['kind'] == 'multitone':
        window = signal.windows.hann(len(y), sym=False)
        output_fft = fft.rfft(y[:, 0] * window)
        reference_fft = fft.rfft(ref[:, 0] * window)
        indices = np.rint(FREQUENCIES * len(y) / rate).astype(int)
        errors = 20 * np.log10(np.abs(output_fft[indices]) / np.maximum(np.abs(reference_fft[indices]) * calibration_gain, 1e-300))
        metrics['passband_max_error_db'] = float(np.max(np.abs(errors)))
        metrics['passband_20khz_db'] = float(errors[-1])
        metrics['passband_db'] = {str(f): float(e) for f, e in zip(FREQUENCIES, errors)}
    if case['kind'] == 'stereo':
        leakage = []
        for channel, wanted, unwanted in ((0, 1000, 1700), (1, 1700, 1000)):
            f, power = signal.periodogram(y[:, channel], rate, window='hann', scaling='spectrum')
            leakage.append(float(10 * np.log10(max(power[np.argmin(abs(f - unwanted))], 1e-300) / max(power[np.argmin(abs(f - wanted))], 1e-300))))
        metrics['worst_channel_leakage_dbc'] = max(leakage)
    if case['kind'] == 'silence':
        metrics['nonzero_samples'] = int(np.count_nonzero(y))
        metrics['silence_rms_dbfs'] = None if not np.any(y) else db(np.sqrt(np.mean(y ** 2)))
    return {'metrics': metrics, 'capture': metadata, 'capture_sha256': digest(prefix.with_suffix('.f32'))}


def cell(records, case, mode, key, digits=2):
    values = [r['metrics'][key] for r in records if r['case'] == case and r['mode'] == mode and r['status'] == 'ok']
    if not values or any(v is None for v in values):
        return 'digital zero' if key == 'silence_rms_dbfs' else 'n/a'
    low, mid, high = min(values), float(np.median(values)), max(values)
    if key == 'worst_channel_leakage_dbc' and high < -180:
        return '<−180'
    if key in ('residual_19khz_dbc', 'residual_15_1khz_dbc') and high < -180:
        return '<−180<br><sub>reporting floor</sub>'
    if key == 'waveform_snr_db' and low > 150:
        return '≥150<br><sub>analysis floor</sub>'
    if key == 'silence_rms_dbfs' and high < -180:
        return '<−180<br><sub>numerical tail</sub>'
    if digits == 0:
        return str(round(mid)) if low == high else f'{round(mid)} ({round(low)}…{round(high)})'
    return f'{mid:.{digits}f}<br><sub>{low:.{digits}f}…{high:.{digits}f}</sub>'


def report(result, readme, output):
    records = result['runs']
    complete = all(r['status'] == 'ok' for r in records)
    lines = [f"**{len(records)} live captures** · {len(result['cases'])} signals / formats · three output paths · {result['repetitions']} repetitions per case.", '',
             f"Music: **{result['music']['title']}**, {result['music']['excerpt_start_seconds']:g}–{result['music']['excerpt_start_seconds'] + 12:g} s from a local lossless stereo file. The same excerpt was tested as 44.1 kHz PCM16 and derived 48/96 kHz PCM24. No music or captured audio is included in the release.", '',
             f"Both actual DirectShow renderers were captured digitally at the same **48 kHz Windows endpoint**. ARE used **{result['driver']['name']}** at **{next((r['capture']['asio_rate'] for r in records if r['mode'] != 'system' and r['status'] == 'ok'), 0) / 1000:g} kHz**, with Keep device sample rate enabled, unity renderer volume, automatic output precision and transition smoothing disabled. System/device settings were unchanged between runs. Captures with audio present before the test started are rejected.", '',
             'Values are medians; small text gives the minimum…maximum across repeated runs. ↑ means higher is better; ↓ means lower is better.', '',
             '| Input / measurement | Unit | System Default (DirectSound) | ARE / r8brain | ARE / SoX Sinc |',
             '| --- | --- | ---: | ---: | ---: |']
    for case in result['cases']:
        kind = case['kind']
        key, unit, label, digits = {
            'music': ('waveform_snr_db', 'dB ↑', 'Music, aligned waveform SNR', 2),
            'tone': ('thdn_dbc', 'dBc ↓', '1 kHz, THD+N', 2),
            'multitone': ('passband_max_error_db', 'dB ↓', '31 tones, worst gain deviation', 4),
            'lowlevel': ('thdn_dbc', 'dBc ↓', '−90 dBFS 1 kHz, THD+N', 2),
            'ultrasonic': ('residual_19khz_dbc', 'dBc ↓', '1 + 29 kHz, 19 kHz residual', 2),
            'stereo': ('worst_channel_leakage_dbc', 'dBc ↓', 'L: 1 kHz / R: 1.7 kHz, channel leakage', 2),
            'silence': ('nonzero_samples', 'samples ↓', 'Digital silence, nonzero samples', 0),
        }[kind]
        values = [cell(records, case['name'], mode, key, digits) for mode in MODES]
        lines.append(f"| {label}, {case['rate'] / 1000:g} kHz / {case['bits']}-bit | {unit} | " + ' | '.join(values) + ' |')
        if kind == 'ultrasonic':
            for extra_key, extra_label in (('residual_15_1khz_dbc', '1 + 29 kHz, 15.1 kHz residual'),
                                           ('thdn_dbc', '1 + 29 kHz, total residual')):
                values = [cell(records, case['name'], mode, extra_key) for mode in MODES]
                lines.append(f"| {extra_label}, 96 kHz / 24-bit | dBc ↓ | " + ' | '.join(values) + ' |')
        if kind == 'silence':
            values = [cell(records, case['name'], mode, 'silence_rms_dbfs') for mode in MODES]
            lines.append('| Digital silence, settled RMS level | dBFS ↓ | ' + ' | '.join(values) + ' |')
    failures = sum(r['status'] != 'ok' for r in records)
    clips = sum(r.get('metrics', {}).get('clipped_samples', 0) for r in records)
    underruns = sum(r.get('capture', {}).get('underruns', 0) for r in records if r['mode'] != 'system')
    overloads = sum(r.get('capture', {}).get('overloads', 0) for r in records if r['mode'] != 'system')
    flags = sum(r.get('capture', {}).get('loopback_discontinuities', 0) for r in records)
    timestamps = sum(r.get('capture', {}).get('timestamp_errors', 0) for r in records)
    lines += ['', '| Reliability check | Observed result |', '| --- | ---: |',
              f'| Completed captures | {len(records) - failures}/{len(records)} |',
              f'| Captured samples at or above digital full scale | {clips} |',
              f'| ARE-reported underruns / overloads | {underruns} / {overloads} |',
              f'| Loopback discontinuity flags, excluding first packet | {flags} |',
              f'| Loopback timestamp error flags | {timestamps} |',
              f'| Pre-test audio / rejected captures | {sum("before the test" in r.get("error", "") for r in records)} |', '',
              'Music SNR is measured against a zero-padded Fourier reference after fitting one shared stereo gain and one fractional delay. It is waveform agreement, not a listening score. THD+N includes source quantization, SRC, driver and Windows-path errors. The two paths include different driver resampling stages; these results do not isolate the converter or measure DAC/analog performance.', '',
              '[Full method, per-run measurements and reproduction](docs/quality/README.md).']
    if not complete:
        lines.insert(0, '**Incomplete experiment: capture failures are recorded in the detailed results.**\n')
    block = '\n'.join(lines)
    text = readme.read_text(encoding='utf-8')
    before, remainder = text.split('<!-- measurement:start -->', 1)
    _, after = remainder.split('<!-- measurement:end -->', 1)
    readme.write_text(before + '<!-- measurement:start -->\n' + block + '\n<!-- measurement:end -->' + after, encoding='utf-8')
    csv_path = output.with_suffix('.csv')
    keys = ['case', 'mode', 'repeat', 'status', 'waveform_snr_db', 'sample_exact_at_integer_alignment',
            'thdn_dbc', 'passband_max_error_db', 'passband_20khz_db', 'residual_19khz_dbc',
            'residual_15_1khz_dbc', 'worst_channel_leakage_dbc', 'nonzero_samples', 'silence_rms_dbfs',
            'calibration_gain_db', 'fractional_alignment_samples', 'captured_peak_dbfs', 'captured_rms_dbfs',
            'captured_peak', 'captured_rms',
            'clipped_samples', 'analysis_frames', 'asio_rate', 'delivered_frames', 'buffer_frames',
            'output_latency_frames', 'underruns', 'overloads', 'loopback_discontinuities', 'timestamp_errors']
    with csv_path.open('w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=keys)
        writer.writeheader()
        for record in records:
            merged = {**record, **record.get('metrics', {}), **record.get('capture', {})}
            if record['mode'] == 'system':
                for key in ('asio_rate', 'delivered_frames', 'buffer_frames', 'output_latency_frames', 'underruns', 'overloads'):
                    merged[key] = None
            writer.writerow({k: merged.get(k) for k in keys})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', action='store_true', help='Play the complete live test matrix')
    parser.add_argument('--music', type=Path, help='Local stereo lossless music file; never copied into the source release')
    parser.add_argument('--music-title')
    parser.add_argument('--music-start', type=float, default=30)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--driver', help='Installed ASIO driver CLSID')
    parser.add_argument('--driver-name', default='FL Studio ASIO')
    parser.add_argument('--repetitions', type=int, default=3)
    parser.add_argument('--gap', type=float, default=1.0, help='Silent gap in seconds between independent captures')
    parser.add_argument('--work-dir', type=Path, default=ROOT / 'out/quality')
    parser.add_argument('--results', type=Path, default=ROOT / 'docs/quality/results.json')
    parser.add_argument('--readme', type=Path, default=ROOT / 'README.md')
    parser.add_argument('--only', help='Single fixture name for a setup check')
    parser.add_argument('--resume', action='store_true', help='Reuse verified capture files in the work folder')
    parser.add_argument('--date', default=date.today().isoformat(), help='User-facing measurement date')
    args = parser.parse_args()
    if args.capture:
        if not args.music or not args.build_dir or not args.driver or args.repetitions < 1 or args.gap < 0:
            parser.error('--capture requires --music, --build-dir, --driver and positive repetitions')
        cases, source = fixtures(args)
        if args.only:
            cases = [c for c in cases if c['name'] == args.only]
            if not cases:
                parser.error('Unknown fixture name')
        dll = args.build_dir.resolve() / 'Release/ARE-Audio-Renderer.dll'
        executable = args.build_dir.resolve() / 'Release/quality_capture.exe'
        result = {'date': args.date, 'renderer_version': '0.1.0.0', 'renderer_sha256': digest(dll),
                  'capture_tool_sha256': digest(executable), 'script_sha256': digest(__file__),
                  'driver': {'name': args.driver_name, 'clsid': args.driver}, 'music': source,
                  'repetitions': args.repetitions, 'inter_capture_gap_seconds': args.gap,
                  'platform': platform.platform(), 'python_version': platform.python_version(),
                  'python_dependencies': {'numpy': np.__version__, 'scipy': scipy.__version__, 'soundfile': sf.__version__},
                  'cases': [{k: v for k, v in c.items() if k not in ('wav', 'reference')} for c in cases], 'runs': []}
        previous_checkpoint = args.work_dir / 'checkpoint.json'
        previous = json.loads(previous_checkpoint.read_text(encoding='utf-8')) if args.resume and previous_checkpoint.exists() else {}
        result['capture_script_sha256'] = previous.get('capture_script_sha256', previous.get('script_sha256', result['script_sha256']))
        result['reused_captures'] = 0
        count = len(cases) * len(MODES) * args.repetitions
        for case in cases:
            for repeat in range(1, args.repetitions + 1):
                for mode in MODES:
                    prefix = args.work_dir.resolve() / f"{case['name']}-{mode}-{repeat}"
                    record = {'case': case['name'], 'mode': mode, 'repeat': repeat, 'status': 'ok'}
                    print(f"[{len(result['runs']) + 1}/{count}] {case['name']} / {mode} / run {repeat}", flush=True)
                    command = [str(executable), 'system' if mode == 'system' else 'are', str(case['wav'].resolve()), str(prefix), str(dll)]
                    if mode != 'system':
                        command += [args.driver, mode]
                    identity = {'input_sha256': case['input_sha256'], 'mode': mode,
                                'renderer_sha256': result['renderer_sha256'],
                                'capture_tool_sha256': result['capture_tool_sha256'], 'driver': args.driver}
                    try:
                        reuse = args.resume and prefix.with_suffix('.f32').exists() and prefix.with_suffix('.json').exists()
                        if reuse:
                            saved = json.loads(prefix.with_suffix('.json').read_text(encoding='utf-8'))
                            if saved.get('capture_config') != identity:
                                raise ValueError('Resume capture configuration does not match the current fixture and binaries')
                            result['reused_captures'] += 1
                        else:
                            if result['runs']:
                                time.sleep(args.gap)
                            completed = subprocess.run(command, capture_output=True, text=True, timeout=75)
                            if completed.returncode:
                                raise RuntimeError(completed.stderr.strip() or completed.stdout.strip())
                            saved = json.loads(prefix.with_suffix('.json').read_text(encoding='utf-8'))
                            saved['capture_config'] = identity
                            prefix.with_suffix('.json').write_text(json.dumps(saved, indent=2) + '\n', encoding='utf-8')
                        record.update(analysis(case, prefix))
                        print('  waveform SNR:', record['metrics']['waveform_snr_db'], 'dB; underruns:', record['capture']['underruns'], flush=True)
                    except (RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
                        record.update(status='failed', error=str(error))
                        print('  FAILED:', error, flush=True)
                    result['runs'].append(record)
                    (args.work_dir / 'checkpoint.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        args.results.parent.mkdir(parents=True, exist_ok=True)
        args.results.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    else:
        result = json.loads(args.results.read_text(encoding='utf-8'))
    report(result, args.readme, args.results)
    print('Results:', args.results, flush=True)
    if any(r['status'] != 'ok' for r in result['runs']):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
