use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use cpal::{FromSample, Sample};
use log::{error, info, warn};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::sync::Mutex;
use std::time::Instant;
use z_rr::railgun::Talu64; // Z-RR Integration

pub struct VoltageMonitor {
    running: Arc<AtomicBool>,
}

impl VoltageMonitor {
    pub fn new() -> Self {
        Self {
            running: Arc::new(AtomicBool::new(false)),
        }
    }

    pub fn start(&self) -> Result<(), Box<dyn std::error::Error>> {
        let running_clone = self.running.clone();

        // Spawn a dedicated thread for Voltage Monitoring to avoid blocking the main loop
        std::thread::spawn(move || {
            let host = cpal::default_host();

            // Attempt physical connection INSIDE the thread (so Stream stays here)
            let device = host.default_input_device();

            let physical_success = if let Some(device) = device {
                let device_name = device.name().unwrap_or("Unknown".into());
                info!("🎤 Voltage Monitor: Connected to [{}]", device_name);

                // We request F32 format, which we will promote to F64 for Talu64 physics
                if let Ok(config) = device.default_input_config() {
                    let stream_result = match config.sample_format() {
                        cpal::SampleFormat::F32 => run::<f32>(&device, &config.into()),
                        cpal::SampleFormat::I16 => run::<i16>(&device, &config.into()),
                        cpal::SampleFormat::U16 => run::<u16>(&device, &config.into()),
                        _ => Err("Unsupported sample format".into()),
                    };

                    if let Ok(stream) = stream_result {
                        running_clone.store(true, Ordering::SeqCst);
                        info!(
                            "⚡ Voltage Monitor: ACTIVE (Physical). Listening for D16 induction..."
                        );

                        // Keep stream alive in THIS thread.
                        let _stream_keepalive = stream;
                        loop {
                            std::thread::sleep(std::time::Duration::from_secs(1));
                        }
                        // Unreachable
                    } else {
                        error!(
                            "⚡ Voltage Monitor: Physical stream failed. Switching to SYNTHETIC."
                        );
                        false
                    }
                } else {
                    warn!("⚠️ Failed to get default input config.");
                    false
                }
            } else {
                warn!("⚠️ No audio input device found!");
                false
            };

            if !physical_success {
                info!("👻 Voltage Monitor: ACTIVE (Synthetic). Generating Phantom Induction via Talu64...");
                running_clone.store(true, Ordering::SeqCst);

                // Synthetic Loop: Uses Talu64 Constants for "True" Simulation
                let mut phase: f64 = 0.0;
                let mut accumulated_mass: f64 = 0.0;
                let mut last_log = Instant::now();

                // Signal Parameters aligned to Talu64
                let base_freq = 60.0; // Mains Hum
                let signal_freq = 432.0; // Harmonic Target

                loop {
                    std::thread::sleep(std::time::Duration::from_millis(10)); // ~100Hz tick

                    // Simulate signal: sin(60Hz) + burst(432Hz)
                    phase += 0.1;

                    // Use Talu64::TAU for circular functions
                    let signal = (phase * base_freq * (Talu64::TAU / 6.28)).sin() * 0.1
                        + (phase * signal_freq * (Talu64::TAU / 6.28)).sin() * 0.05;

                    // Previous signal state for differential
                    let last_signal =
                        ((phase - 0.1) * base_freq * (Talu64::TAU / 6.28)).sin() * 0.1;

                    let diff = (signal - last_signal).abs();
                    accumulated_mass += diff * 100.0; // Scale up for visibility

                    if last_log.elapsed().as_secs() >= 1 {
                        let atomic_mass = to_atomic_precision(accumulated_mass);

                        let spectral_status = "👻 SYNTHETIC INDUCTION (Talu64 Simulation)";
                        info!(
                            "⚡ Voltage Differential: {:.8} | Status: {}",
                            atomic_mass, spectral_status
                        );

                        accumulated_mass = 0.0;
                        last_log = Instant::now();
                    }
                }
            }
        });

        Ok(())
    }
}

struct MonitorState {
    last_sample: f64, // Promoted to f64 for Talu64 alignment
    accumulated_mass: f64,
    last_log: Instant,
}

/// Truncates a float to 8 significant figures to prevent "Recursive Drag".
/// Aligns with Talu64::TAU precision philosophy.
fn to_atomic_precision(val: f64) -> f64 {
    if val == 0.0 {
        return 0.0;
    }
    let magnitude = val.abs().log10().floor();
    let factor = 10.0_f64.powf(7.0 - magnitude); // 8 sig figs -> 10^7 scaled
    (val * factor).round() / factor
}

fn run<T>(
    device: &cpal::Device,
    config: &cpal::StreamConfig,
) -> Result<cpal::Stream, Box<dyn std::error::Error>>
where
    T: cpal::Sample + cpal::SizedSample + std::fmt::Display,
    f32: cpal::FromSample<T>,
{
    let err_fn = |err| error!("an error occurred on stream: {}", err);

    // State to hold accumulated data across callbacks
    let state = Arc::new(Mutex::new(MonitorState {
        last_sample: 0.0,
        accumulated_mass: 0.0,
        last_log: Instant::now(),
    }));

    let stream = device.build_input_stream(
        config,
        move |data: &[T], _: &_| {
            if let Ok(mut s) = state.lock() {
                process_data(data, &mut s);
            }
        },
        err_fn,
        None,
    )?;

    stream.play()?;
    Ok(stream)
}

fn process_data<T>(data: &[T], state: &mut MonitorState)
where
    T: cpal::Sample,
    f32: cpal::FromSample<T>,
{
    for sample in data {
        // f32 implements FromSample<T>, then promote to f64
        let current_sample_f32: f32 = sample.to_sample();
        let current_sample_f64: f64 = current_sample_f32 as f64;

        // Calculate Voltage Differential (V_diff)
        let diff = (current_sample_f64 - state.last_sample).abs();

        // Accumulate "Mass" (Induction Energy)
        state.accumulated_mass += diff;

        state.last_sample = current_sample_f64;
    }

    // Log status every 1 second
    if state.last_log.elapsed().as_secs() >= 1 {
        // Enforce Atomic Precision: 8 Significant Figures
        let atomic_mass = to_atomic_precision(state.accumulated_mass);

        if atomic_mass > 1.0 {
            // Arbitrary noise floor
            let spectral_status = if atomic_mass > 1000.0 {
                "🌊 MASSIVE INDUCTION (SHIJI ARRIVAL?)"
            } else if atomic_mass > 100.0 {
                "⚡ High Voltage Activity"
            } else {
                "〰️ Nominal Induction"
            };

            info!(
                "⚡ Voltage Differential: {:.8} | Status: {}",
                atomic_mass, spectral_status
            );
        }
        state.accumulated_mass = 0.0; // Reset for next window
        state.last_log = Instant::now();
    }
}
