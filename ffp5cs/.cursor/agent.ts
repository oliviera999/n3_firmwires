import { agent, getFile, exec, watchFile } from "cursor/agent";

agent("ESP32 Intelligent Assistant", async ({ status, run }, { fs }) => {
  status("🚀 Agent ESP32 Intelligent Assistant démarré");

  // Surveillance des logs critiques
  const logPath = "pythonserial/esp32_logs.txt";
  
  await watchFile(logPath, async (changes) => {
    const newLogs = changes.after;
    const lines = newLogs.split("\n");
    
    // Détection d'erreurs critiques
    const criticalErrors = lines.filter(line =>
      /Guru Meditation|panic:|assert failed|core dumped|Brownout|Stack overflow|Heap corruption/.test(line)
    );
    
    // Détection d'avertissements importants
    const warnings = lines.filter(line =>
      /WARNING|WARN|timeout|disconnect|reconnect|memory|heap/.test(line)
    );
    
    // Détection de patterns de performance
    const performanceIssues = lines.filter(line =>
      /slow|delay|blocking|watchdog|task/.test(line)
    );

    if (criticalErrors.length > 0) {
      status(`🚨 ${criticalErrors.length} erreurs critiques détectées !`);
      
      const appFile = await getFile("src/app.cpp");
      const configFile = await getFile("include/config.h");
      
      await run(`
        🚨 ERREURS CRITIQUES ESP32 DÉTECTÉES :
        ${criticalErrors.join("\n")}
        
        📋 CONTEXTE DU CODE :
        - app.cpp: ${appFile.content.substring(0, 2000)}...
        - config.h: ${configFile.content.substring(0, 1000)}...
        
        🔧 ACTIONS REQUISES :
        1. Analyser les erreurs et proposer des corrections immédiates
        2. Vérifier la gestion mémoire et les allocations
        3. Contrôler les timeouts et la gestion watchdog
        4. Suggérer des optimisations de stabilité
        
        Priorité: CRITIQUE - Intervention immédiate nécessaire
      `);
    }
    
    if (warnings.length > 0) {
      status(`⚠️ ${warnings.length} avertissements détectés`);
      
      await run(`
        ⚠️ AVERTISSEMENTS ESP32 :
        ${warnings.slice(0, 10).join("\n")}
        
        📊 ANALYSE :
        - Vérifier la stabilité des connexions WiFi/WebSocket
        - Contrôler l'utilisation mémoire
        - Optimiser les timeouts
        
        Priorité: MOYENNE - Surveillance recommandée
      `);
    }
    
    if (performanceIssues.length > 0) {
      status(`⚡ ${performanceIssues.length} problèmes de performance détectés`);
      
      await run(`
        ⚡ PROBLÈMES DE PERFORMANCE :
        ${performanceIssues.slice(0, 5).join("\n")}
        
        🎯 OPTIMISATIONS SUGGÉRÉES :
        - Réduire les délais bloquants
        - Optimiser la gestion des tâches
        - Améliorer la gestion watchdog
        
        Priorité: FAIBLE - Optimisation recommandée
      `);
    }
  });

  // Surveillance des fichiers de configuration
  await watchFile("platformio.ini", async (changes) => {
    status("📝 Configuration PlatformIO modifiée");
    
    await run(`
      📝 CONFIGURATION PLATFORMIO MODIFIÉE
      
      Vérifier :
      - Cohérence des environnements (wroom-test, s3-test, etc.)
      - Versions des bibliothèques
      - Flags de compilation
      - Configuration des partitions
      
      Suggérer des optimisations si nécessaire.
    `);
  });

  // Surveillance du code source principal (point d'entrée setup/loop)
  await watchFile("src/app.cpp", async (changes) => {
    const content = changes.after;
    
    // Détection de patterns problématiques
    const problematicPatterns = [
      { pattern: /delay\([0-9]+\)/, message: "⚠️ Délai bloquant détecté - utiliser vTaskDelay()" },
      { pattern: /String\s+\w+/, message: "⚠️ Utilisation de String Arduino - préférer char[]" },
      { pattern: /malloc\(|new\s+\w+/, message: "⚠️ Allocation dynamique - vérifier la libération" },
      { pattern: /while\s*\(\s*true\s*\)/, message: "⚠️ Boucle infinie - ajouter watchdog reset" }
    ];
    
    const issues = problematicPatterns.filter(({ pattern }) => pattern.test(content));
    
    if (issues.length > 0) {
      status(`🔍 ${issues.length} patterns problématiques détectés dans app.cpp`);
      
      await run(`
        🔍 PATTERNS PROBLÉMATIQUES DÉTECTÉS :
        ${issues.map(i => i.message).join("\n")}
        
        📋 RECOMMANDATIONS :
        - Remplacer delay() par vTaskDelay() pour éviter le blocage
        - Utiliser char[] au lieu de String pour éviter la fragmentation
        - Vérifier la libération de toute allocation mémoire
        - Ajouter esp_task_wdt_reset() dans les boucles longues
        
        Priorité: MOYENNE - Bonnes pratiques ESP32
      `);
    }
  });

  // Surveillance de la version
  await watchFile("include/config.h", async (changes) => {
    const content = changes.after;
    const versionMatch = content.match(/VERSION\s+["']([0-9.]+)["']/);
    
    if (versionMatch) {
      status(`📌 Version détectée: ${versionMatch[1]}`);
      
      await run(`
        📌 VERSION DU PROJET: ${versionMatch[1]}
        
        ✅ Vérifications :
        - Version incrémentée selon les règles du projet
        - Documentation des changements mise à jour
        - Tests de stabilité effectués
        
        Rappel: Toujours incrémenter la version après modifications !
      `);
    }
  });

  status("✅ Agent ESP32 Intelligent Assistant opérationnel");
});
