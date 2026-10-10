// Stub ESP_Mail_Client minimal pour tests natifs de n3_mail.
// n3_mail.cpp inclut <ESP_Mail_Client.h> au niveau de la TU pour n3MailSendText.
// Les tests ne ciblent QUE les builders de corps de mail (snprintf pur) ; ce stub
// existe uniquement pour que la TU compile (n3MailSendText n'est pas exercé).
#pragma once
#include <Arduino.h>

enum class Content_Transfer_Encoding { enc_7bit };

namespace esp_mail_smtp_priority {
enum { esp_mail_smtp_priority_low };
}

struct ESPMailServer {
  const char* host_name = nullptr;
  uint16_t port = 0;
};
struct ESPMailLogin {
  const char* email = nullptr;
  const char* password = nullptr;
};
struct Session_Config {
  ESPMailServer server;
  ESPMailLogin login;
};

struct ESPMailSenderField {
  const char* name = nullptr;
  const char* email = nullptr;
};
struct ESPMailTextField {
  const char* content = nullptr;
  const char* charSet = nullptr;
  Content_Transfer_Encoding transfer_encoding = Content_Transfer_Encoding::enc_7bit;
};
struct SMTP_Message {
  ESPMailSenderField sender;
  ESPMailTextField text;
  const char* subject = nullptr;
  int priority = -1;  // -1 = jamais positionne (permet de tester l'opt-in priorite)
  const char* lastRecipientName = nullptr;
  const char* lastRecipientEmail = nullptr;
  void addRecipient(const char* name, const char* email) {
    lastRecipientName = name;
    lastRecipientEmail = email;
  }
};

// Resultat d'envoi (miroir minimal de SendingResult / SMTP_Result) : le vrai client
// enregistre `completed=true` des que le serveur repond 250 au DATA, AVANT la
// cloture de session (cf. n3MailLastResultAccepted).
struct ESPMailSmtpResult {
  bool completed = false;
};
struct ESPMailSendingResult {
  bool hasItem = false;
  ESPMailSmtpResult item;
  void clear() {
    hasItem = false;
    item.completed = false;
  }
  size_t size() { return hasItem ? 1 : 0; }
  ESPMailSmtpResult getItem(size_t /*index*/) { return item; }
};

class SMTPSession {
 public:
  ESPMailSendingResult sendingResult;
  bool connect(Session_Config* /*cfg*/) { return false; }
  String errorReason() { return String("(stub) erreur SMTP simulee"); }
};

// Client stub : par defaut sendMail echoue (comportement historique du stub, ne
// casse aucun test existant qui n'envoie pas). Les tests qui veulent exercer le
// chemin succes/echec du builder de message peuvent regler `nextSendResult` et
// inspecter `lastMessage` (capture du dernier SMTP_Message envoye).
// `nextRecordedResult` : -1 = aucun resultat enregistre (echec avant DATA, ex.
// connexion/auth), 0 = message refuse, 1 = message accepte (250). Permet de simuler
// le faux negatif « accepte puis closeSession() en echec » (sendMail()==false).
class ESPMailClient {
 public:
  bool nextSendResult = false;
  int nextRecordedResult = -1;
  const SMTP_Message* lastMessage = nullptr;
  int sendMailCallCount = 0;
  bool sendMail(SMTPSession* smtp, SMTP_Message* msg) {
    lastMessage = msg;
    ++sendMailCallCount;
    if (smtp != nullptr && nextRecordedResult >= 0) {
      smtp->sendingResult.hasItem = true;
      smtp->sendingResult.item.completed = (nextRecordedResult == 1);
    }
    return nextSendResult;
  }
};
inline ESPMailClient MailClient;
