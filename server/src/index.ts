import express from "express";
import { OpenAI } from "openai";
import { execSync } from "child_process";
import { writeFileSync, readFileSync, unlinkSync } from "fs";
import { tmpdir } from "os";
import { join } from "path";

// ── Config ────────────────────────────────────────────
const PORT = parseInt(process.env.PORT || "3001");
const OPENAI_API_KEY = process.env.OPENAI_API_KEY || "";
const TTS_VOICE = process.env.TTS_VOICE || "en-US-GuyNeural";
const SYSTEM_PROMPT = process.env.SYSTEM_PROMPT || `You are Jarvis, a helpful voice assistant running on an ESP32 device. 
Keep responses concise (1-3 sentences) since they will be spoken aloud. 
Be natural and conversational.`;

const openai = new OpenAI({ apiKey: OPENAI_API_KEY });
const app = express();

// Conversation history (in-memory, per session)
const conversationHistory: Array<{ role: "user" | "assistant" | "system"; content: string }> = [
  { role: "system", content: SYSTEM_PROMPT },
];

// ── POST /conversation ────────────────────────────────
// Receives raw PCM audio from ESP32, returns PCM audio response
app.post("/conversation", express.raw({ type: "application/octet-stream", limit: "5mb" }), async (req, res) => {
  const startTime = Date.now();

  try {
    const pcmBuffer = req.body as Buffer;
    const sampleRate = parseInt(req.headers["x-sample-rate"] as string) || 16000;
    const bitDepth = parseInt(req.headers["x-bit-depth"] as string) || 16;

    console.log(`🎤 Received ${pcmBuffer.length} bytes (${sampleRate}Hz, ${bitDepth}bit)`);

    // 1. Convert PCM to WAV for Whisper
    const wavPath = join(tmpdir(), `voice-${Date.now()}.wav`);
    const wavBuffer = pcmToWav(pcmBuffer, sampleRate, 1, bitDepth);
    writeFileSync(wavPath, wavBuffer);

    // 2. Transcribe with Whisper
    console.log("📝 Transcribing...");
    const transcription = await openai.audio.transcriptions.create({
      file: new File([readFileSync(wavPath)], "audio.wav", { type: "audio/wav" }),
      model: "whisper-1",
      language: "es",
    });
    unlinkSync(wavPath);

    const userText = transcription.text.trim();
    console.log(`📝 User: "${userText}"`);

    if (!userText) {
      res.status(400).send("No speech detected");
      return;
    }

    // 3. Get LLM response
    conversationHistory.push({ role: "user", content: userText });

    console.log("🤖 Thinking...");
    const chatResponse = await openai.chat.completions.create({
      model: "gpt-4o-mini",
      messages: conversationHistory,
      max_tokens: 200,
      temperature: 0.7,
    });

    const assistantText = chatResponse.choices[0]?.message?.content || "No pude generar una respuesta.";
    conversationHistory.push({ role: "assistant", content: assistantText });

    // Keep history manageable (last 20 messages + system)
    if (conversationHistory.length > 21) {
      conversationHistory.splice(1, conversationHistory.length - 21);
    }

    console.log(`🤖 Assistant: "${assistantText}"`);

    // 4. TTS → PCM
    console.log("🔊 Generating speech...");
    const pcmResponse = await textToSpeechPCM(assistantText, sampleRate);

    const elapsed = Date.now() - startTime;
    console.log(`✅ Response ready (${elapsed}ms, ${pcmResponse.length} bytes audio)`);

    res.set("Content-Type", "application/octet-stream");
    res.set("X-Transcription", encodeURIComponent(userText));
    res.set("X-Response-Text", encodeURIComponent(assistantText));
    res.send(pcmResponse);
  } catch (error) {
    console.error("❌ Error:", error);
    res.status(500).send("Internal error");
  }
});

// ── POST /say ─────────────────────────────────────────
// Direct TTS: send text, get PCM audio back
app.post("/say", express.json(), async (req, res) => {
  try {
    const { text, sampleRate = 16000 } = req.body;
    if (!text) {
      res.status(400).json({ error: "text required" });
      return;
    }

    console.log(`🔊 Say: "${text}"`);
    const pcm = await textToSpeechPCM(text, sampleRate);

    res.set("Content-Type", "application/octet-stream");
    res.send(pcm);
  } catch (error) {
    console.error("❌ TTS error:", error);
    res.status(500).send("TTS error");
  }
});

// ── GET /status ───────────────────────────────────────
app.get("/status", (_req, res) => {
  res.json({
    status: "ok",
    conversationLength: conversationHistory.length - 1,
    uptime: process.uptime(),
  });
});

// ── POST /reset ───────────────────────────────────────
app.post("/reset", (_req, res) => {
  conversationHistory.splice(1);
  console.log("🔄 Conversation reset");
  res.json({ status: "reset" });
});

// ── PCM to WAV conversion ─────────────────────────────
function pcmToWav(pcm: Buffer, sampleRate: number, channels: number, bitDepth: number): Buffer {
  const byteRate = sampleRate * channels * (bitDepth / 8);
  const blockAlign = channels * (bitDepth / 8);
  const header = Buffer.alloc(44);

  header.write("RIFF", 0);
  header.writeUInt32LE(36 + pcm.length, 4);
  header.write("WAVE", 8);
  header.write("fmt ", 12);
  header.writeUInt32LE(16, 16);
  header.writeUInt16LE(1, 20);         // PCM
  header.writeUInt16LE(channels, 22);
  header.writeUInt32LE(sampleRate, 24);
  header.writeUInt32LE(byteRate, 28);
  header.writeUInt16LE(blockAlign, 32);
  header.writeUInt16LE(bitDepth, 34);
  header.write("data", 36);
  header.writeUInt32LE(pcm.length, 40);

  return Buffer.concat([header, pcm]);
}

// ── TTS using edge-tts (same as OpenClaw) ─────────────
async function textToSpeechPCM(text: string, targetSampleRate: number): Promise<Buffer> {
  const tmpMp3 = join(tmpdir(), `tts-${Date.now()}.mp3`);
  const tmpPcm = join(tmpdir(), `tts-${Date.now()}.pcm`);

  try {
    // Generate MP3 with edge-tts
    execSync(
      `edge-tts --voice "${TTS_VOICE}" --text "${text.replace(/"/g, '\\"')}" --write-media "${tmpMp3}"`,
      { timeout: 15000 }
    );

    // Convert to raw PCM with ffmpeg
    execSync(
      `ffmpeg -y -i "${tmpMp3}" -f s16le -acodec pcm_s16le -ar ${targetSampleRate} -ac 1 "${tmpPcm}"`,
      { timeout: 10000, stdio: "pipe" }
    );

    const pcm = readFileSync(tmpPcm);
    return pcm;
  } finally {
    try { unlinkSync(tmpMp3); } catch {}
    try { unlinkSync(tmpPcm); } catch {}
  }
}

// ── Start ─────────────────────────────────────────────
app.listen(PORT, "0.0.0.0", () => {
  console.log(`\n🚀 ESP32 Voice Agent Server running on port ${PORT}`);
  console.log(`   POST /conversation  — audio in → audio out`);
  console.log(`   POST /say           — text in → audio out`);
  console.log(`   GET  /status        — server status`);
  console.log(`   POST /reset         — clear conversation\n`);
});
