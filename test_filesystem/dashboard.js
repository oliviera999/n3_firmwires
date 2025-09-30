// Dashboard JS - Version OTA Test
console.log('🚀 Dashboard OTA Test chargé !');

document.addEventListener('DOMContentLoaded', function() {
    // Afficher la version OTA
    const versionElement = document.getElementById('ota-version');
    if (versionElement) {
        versionElement.textContent = '10.12-OTA-TEST';
    }
    
    // Animation de chargement
    const cards = document.querySelectorAll('.card');
    cards.forEach((card, index) => {
        card.style.opacity = '0';
        card.style.transform = 'translateY(20px)';
        
        setTimeout(() => {
            card.style.transition = 'all 0.3s ease';
            card.style.opacity = '1';
            card.style.transform = 'translateY(0)';
        }, index * 100);
    });
    
    console.log('✅ Dashboard OTA Test initialisé');
});