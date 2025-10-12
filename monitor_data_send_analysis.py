#!/usr/bin/env python3
"""
Monitoring ESP32 - Analyse envoi de données vers serveur
Durée: 5 minutes
Focus: Détection des envois HTTP POST/GET et points d'attention
"""

import serial
import time
import sys
import re
from datetime import datetime

# Configuration
PORT = "COM6"
BAUDRATE = 115200
DURATION = 300  # 5 minutes en secondes

# Patterns à surveiller
PATTERNS = {
    'envoi': re.compile(r'(POST|GET|Sending|Envoi|sendData|HTTP|serveur|server|INSERT|UPDATE|données|data)', re.IGNORECASE),
    'critique': re.compile(r'(Guru|Panic|Brownout|crash|reset|exception|abort)', re.IGNORECASE),
    'attention': re.compile(r'(watchdog|timeout|warning|erreur|error|failed|échec)', re.IGNORECASE),
    'memoire': re.compile(r'(heap|free|malloc|PSRAM|memory)', re.IGNORECASE),
    'wifi': re.compile(r'(WiFi|SSID|connected|disconnected|reconnect)', re.IGNORECASE),
    'websocket': re.compile(r'(WebSocket|ws|client|connect)', re.IGNORECASE),
}

# Statistiques
stats = {
    'envois_detectes': 0,
    'erreurs_critiques': 0,
    'warnings': 0,
    'total_lignes': 0
}

def print_colored(text, color_code):
    """Affiche du texte coloré dans le terminal"""
    colors = {
        'red': '\033[91m',
        'green': '\033[92m',
        'yellow': '\033[93m',
        'cyan': '\033[96m',
        'reset': '\033[0m'
    }
    print(f"{colors.get(color_code, '')}{text}{colors['reset']}")

def analyze_line(line, log_file):
    """Analyse une ligne de log et détecte les patterns importants"""
    timestamp = datetime.now().strftime('%H:%M:%S.%f')[:-3]
    full_line = f"[{timestamp}] {line}"
    
    # Écriture dans le fichier log
    log_file.write(full_line + '\n')
    log_file.flush()
    
    # Affichage console
    print(full_line, end='')
    
    # Analyse des patterns
    if PATTERNS['envoi'].search(line):
        stats['envois_detectes'] += 1
        print_colored("  >>> ENVOI SERVEUR DETECTE <<<", 'cyan')
    
    if PATTERNS['critique'].search(line):
        stats['erreurs_critiques'] += 1
        print_colored("  !!! ERREUR CRITIQUE !!!", 'red')
    
    if PATTERNS['attention'].search(line):
        stats['warnings'] += 1
        print_colored("  ! ATTENTION !", 'yellow')
    
    stats['total_lignes'] += 1

def main():
    log_filename = f"monitor_data_send_5min_{datetime.now().strftime('%Y-%m-%d_%H-%M-%S')}.log"
    
    print_colored(f"\n=== Monitoring ESP32 sur {PORT} pendant {DURATION}s ===", 'cyan')
    print_colored("Focus: Envoi données serveur + Points d'attention", 'yellow')
    print_colored(f"Log: {log_filename}\n", 'green')
    
    try:
        # Ouverture du port série
        ser = serial.Serial(PORT, BAUDRATE, timeout=1)
        time.sleep(2)  # Attendre la stabilisation
        
        print_colored(f"[{datetime.now().strftime('%H:%M:%S')}] Monitoring démarré...\n", 'green')
        
        start_time = time.time()
        
        with open(log_filename, 'w', encoding='utf-8') as log_file:
            log_file.write(f"=== Monitoring ESP32 - {datetime.now().strftime('%Y-%m-%d %H:%M:%S')} ===\n")
            log_file.write(f"Port: {PORT}, Baudrate: {BAUDRATE}, Durée: {DURATION}s\n")
            log_file.write("="*80 + "\n\n")
            
            while (time.time() - start_time) < DURATION:
                if ser.in_waiting > 0:
                    try:
                        line = ser.readline().decode('utf-8', errors='ignore').strip()
                        if line:
                            analyze_line(line, log_file)
                    except UnicodeDecodeError:
                        pass
                    except Exception as e:
                        print_colored(f"Erreur lecture: {e}", 'red')
                else:
                    time.sleep(0.1)
        
        # Fermeture propre
        ser.close()
        
        # Affichage des statistiques
        elapsed = time.time() - start_time
        print_colored(f"\n{'='*80}", 'cyan')
        print_colored("=== STATISTIQUES DU MONITORING ===", 'cyan')
        print_colored(f"{'='*80}", 'cyan')
        print_colored(f"Durée totale: {elapsed:.1f}s", 'green')
        print_colored(f"Lignes analysées: {stats['total_lignes']}", 'green')
        print_colored(f"Envois de données détectés: {stats['envois_detectes']}", 'cyan')
        print_colored(f"Erreurs critiques: {stats['erreurs_critiques']}", 'red')
        print_colored(f"Warnings/Attentions: {stats['warnings']}", 'yellow')
        print_colored(f"\nLog sauvegardé: {log_filename}\n", 'green')
        
    except serial.SerialException as e:
        print_colored(f"\nErreur port série: {e}", 'red')
        print_colored("Suggestion: Fermez tous les moniteurs série actifs et réessayez", 'yellow')
        sys.exit(1)
    except KeyboardInterrupt:
        print_colored("\n\nMonitoring interrompu par l'utilisateur", 'yellow')
        sys.exit(0)
    except Exception as e:
        print_colored(f"\nErreur inattendue: {e}", 'red')
        sys.exit(1)

if __name__ == "__main__":
    main()

