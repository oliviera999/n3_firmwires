#!/usr/bin/env python3
"""
Analyseur de logs de démarrage ESP32 FFP3
Capture et analyse les logs pendant 3 minutes après flash complet
"""

import serial
import time
import datetime
import re
import json
from collections import defaultdict, Counter

class ESP32LogAnalyzer:
    def __init__(self, port='COM5', baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.logs = []
        self.start_time = None
        self.end_time = None
        
    def connect(self):
        """Établit la connexion série"""
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=1)
            print(f"✅ Connexion établie sur {self.port} à {self.baudrate} baud")
            return True
        except Exception as e:
            print(f"❌ Erreur connexion: {e}")
            return False
    
    def monitor(self, duration_seconds=180):
        """Monitore les logs pendant la durée spécifiée"""
        if not self.connect():
            return False
            
        print(f"🔍 Début du monitoring pour {duration_seconds} secondes...")
        self.start_time = datetime.datetime.now()
        
        try:
            while (datetime.datetime.now() - self.start_time).total_seconds() < duration_seconds:
                if self.ser.in_waiting > 0:
                    line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        timestamp = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
                        self.logs.append({
                            'timestamp': timestamp,
                            'line': line,
                            'elapsed': (datetime.datetime.now() - self.start_time).total_seconds()
                        })
                        print(f"[{timestamp}] {line}")
                time.sleep(0.01)
                
        except KeyboardInterrupt:
            print("\n⏹️ Monitoring interrompu par l'utilisateur")
        except Exception as e:
            print(f"❌ Erreur pendant le monitoring: {e}")
        finally:
            self.end_time = datetime.datetime.now()
            self.ser.close()
            print(f"✅ Monitoring terminé après {(self.end_time - self.start_time).total_seconds():.1f}s")
            
        return True
    
    def analyze_boot_sequence(self):
        """Analyse la séquence de démarrage"""
        analysis = {
            'total_logs': len(self.logs),
            'duration': (self.end_time - self.start_time).total_seconds() if self.end_time else 0,
            'boot_phases': [],
            'errors': [],
            'warnings': [],
            'wifi_events': [],
            'sensor_readings': [],
            'web_server_events': [],
            'memory_usage': [],
            'performance_metrics': {}
        }
        
        # Analyse des phases de démarrage
        boot_phases = [
            (r'rst:0x[0-9a-f]+', 'Reset'),
            (r'Guru Meditation Error', 'Guru Error'),
            (r'ets_main', 'ETS Main'),
            (r'ESP32', 'ESP32 Init'),
            (r'WiFi', 'WiFi Init'),
            (r'HTTP server started', 'Web Server'),
            (r'LittleFS', 'File System'),
            (r'OTA', 'OTA Manager'),
            (r'WebSocket', 'WebSocket'),
            (r'Setup complete', 'Setup Complete')
        ]
        
        for log in self.logs:
            line = log['line']
            elapsed = log['elapsed']
            
            # Détection des phases
            for pattern, phase_name in boot_phases:
                if re.search(pattern, line, re.IGNORECASE):
                    analysis['boot_phases'].append({
                        'phase': phase_name,
                        'timestamp': log['timestamp'],
                        'elapsed': elapsed,
                        'line': line
                    })
                    break
            
            # Détection des erreurs
            if any(keyword in line.lower() for keyword in ['error', 'failed', 'exception', 'crash']):
                analysis['errors'].append({
                    'timestamp': log['timestamp'],
                    'elapsed': elapsed,
                    'line': line
                })
            
            # Détection des warnings
            if 'warning' in line.lower() or 'warn' in line.lower():
                analysis['warnings'].append({
                    'timestamp': log['timestamp'],
                    'elapsed': elapsed,
                    'line': line
                })
            
            # Événements WiFi
            if any(keyword in line.lower() for keyword in ['wifi', 'connected', 'disconnected', 'ip']):
                analysis['wifi_events'].append({
                    'timestamp': log['timestamp'],
                    'elapsed': elapsed,
                    'line': line
                })
            
            # Lectures de capteurs
            if any(keyword in line.lower() for keyword in ['sensor', 'temp', 'humidity', 'water', 'air']):
                analysis['sensor_readings'].append({
                    'timestamp': log['timestamp'],
                    'elapsed': elapsed,
                    'line': line
                })
            
            # Événements serveur web
            if any(keyword in line.lower() for keyword in ['http', 'request', 'response', 'client']):
                analysis['web_server_events'].append({
                    'timestamp': log['timestamp'],
                    'elapsed': elapsed,
                    'line': line
                })
            
            # Usage mémoire
            if 'free heap' in line.lower() or 'heap' in line.lower():
                analysis['memory_usage'].append({
                    'timestamp': log['timestamp'],
                    'elapsed': elapsed,
                    'line': line
                })
        
        return analysis
    
    def generate_report(self, analysis):
        """Génère un rapport d'analyse détaillé"""
        report = f"""
# 📊 RAPPORT D'ANALYSE ESP32 FFP3 - {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}

## 🔧 Configuration
- **Port**: {self.port}
- **Baudrate**: {self.baudrate}
- **Durée monitoring**: {analysis['duration']:.1f} secondes
- **Total logs**: {analysis['total_logs']} lignes

## 🚀 Séquence de Démarrage
"""
        
        # Phases de démarrage
        if analysis['boot_phases']:
            report += "\n### Phases détectées:\n"
            for phase in analysis['boot_phases']:
                report += f"- **{phase['phase']}**: {phase['timestamp']} (+{phase['elapsed']:.2f}s)\n"
                report += f"  `{phase['line']}`\n\n"
        else:
            report += "\n❌ Aucune phase de démarrage détectée\n"
        
        # Erreurs
        if analysis['errors']:
            report += f"\n## ❌ Erreurs ({len(analysis['errors'])})\n"
            for error in analysis['errors'][:10]:  # Limite à 10 erreurs
                report += f"- **{error['timestamp']}** (+{error['elapsed']:.2f}s): `{error['line']}`\n"
        else:
            report += "\n## ✅ Aucune erreur détectée\n"
        
        # Warnings
        if analysis['warnings']:
            report += f"\n## ⚠️ Warnings ({len(analysis['warnings'])})\n"
            for warning in analysis['warnings'][:5]:  # Limite à 5 warnings
                report += f"- **{warning['timestamp']}**: `{warning['line']}`\n"
        
        # WiFi
        if analysis['wifi_events']:
            report += f"\n## 📶 Événements WiFi ({len(analysis['wifi_events'])})\n"
            for event in analysis['wifi_events'][:5]:
                report += f"- **{event['timestamp']}**: `{event['line']}`\n"
        
        # Capteurs
        if analysis['sensor_readings']:
            report += f"\n## 🌡️ Lectures Capteurs ({len(analysis['sensor_readings'])})\n"
            for reading in analysis['sensor_readings'][:5]:
                report += f"- **{reading['timestamp']}**: `{reading['line']}`\n"
        
        # Serveur Web
        if analysis['web_server_events']:
            report += f"\n## 🌐 Serveur Web ({len(analysis['web_server_events'])})\n"
            for event in analysis['web_server_events'][:5]:
                report += f"- **{event['timestamp']}**: `{event['line']}`\n"
        
        # Mémoire
        if analysis['memory_usage']:
            report += f"\n## 💾 Usage Mémoire ({len(analysis['memory_usage'])})\n"
            for mem in analysis['memory_usage'][:3]:
                report += f"- **{mem['timestamp']}**: `{mem['line']}`\n"
        
        # Métriques de performance
        report += f"\n## 📈 Métriques de Performance\n"
        if analysis['boot_phases']:
            first_phase = analysis['boot_phases'][0]
            last_phase = analysis['boot_phases'][-1]
            boot_time = last_phase['elapsed'] - first_phase['elapsed']
            report += f"- **Temps de démarrage**: {boot_time:.2f}s\n"
        
        report += f"- **Logs par seconde**: {analysis['total_logs'] / analysis['duration']:.1f}\n"
        report += f"- **Taux d'erreurs**: {len(analysis['errors']) / analysis['total_logs'] * 100:.2f}%\n"
        
        # Recommandations
        report += f"\n## 💡 Recommandations\n"
        if analysis['errors']:
            report += "- ⚠️ Des erreurs ont été détectées, vérifier la configuration\n"
        if len(analysis['boot_phases']) < 5:
            report += "- ⚠️ Séquence de démarrage incomplète\n"
        if analysis['duration'] < 60:
            report += "- ⚠️ Monitoring trop court, étendre à 5+ minutes\n"
        if not analysis['wifi_events']:
            report += "- ⚠️ Aucun événement WiFi détecté\n"
        if not analysis['sensor_readings']:
            report += "- ⚠️ Aucune lecture de capteur détectée\n"
        
        return report

def main():
    print("🔍 Analyseur de logs ESP32 FFP3")
    print("=" * 50)
    
    analyzer = ESP32LogAnalyzer()
    
    # Monitoring pendant 3 minutes
    if analyzer.monitor(180):
        print("\n📊 Analyse des logs...")
        analysis = analyzer.analyze_boot_sequence()
        
        # Génération du rapport
        report = analyzer.generate_report(analysis)
        
        # Sauvegarde du rapport
        timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        report_file = f"monitor_analysis_{timestamp}.md"
        
        with open(report_file, 'w', encoding='utf-8') as f:
            f.write(report)
        
        print(f"\n✅ Rapport sauvegardé: {report_file}")
        print("\n" + "="*50)
        print(report)
        
        # Sauvegarde des logs bruts
        logs_file = f"monitor_logs_{timestamp}.json"
        with open(logs_file, 'w', encoding='utf-8') as f:
            json.dump(analyzer.logs, f, indent=2, ensure_ascii=False)
        print(f"📄 Logs bruts sauvegardés: {logs_file}")
        
    else:
        print("❌ Échec du monitoring")

if __name__ == "__main__":
    main()
