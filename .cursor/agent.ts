import { agent, getFile, exec, watchFile } from "cursor/agent";

agent("ESP32 Error Watchdog", async ({ status, run }, { fs }) => {
  status("🔍 Lecture des logs critiques ESP32…");

  const logPath = "pythonserial/esp32_logs.txt";

  await watchFile(logPath, async (changes) => {
    const newLogs = changes.after;
    const criticalErrors = newLogs.split("\n").filter(line =>
      /Guru Meditation|panic:|assert failed|core dumped|Brownout/.test(line)
    );

    if (criticalErrors.length > 0) {
      status(`⚠️ ${criticalErrors.length} erreurs détectées. Analyse du code…`);

      const mainFile = await getFile("src/main.cpp"); // ou main.ino
      const context = mainFile.content;

      const suggestions = await run(`
        Voici des erreurs série détectées :
        ${criticalErrors.join("\n")}

        Analyse le code suivant et propose ou applique des corrections :
        ${context}
      `);

      status("✅ Analyse terminée. Vérifie les suggestions.");
    }
  });
});
