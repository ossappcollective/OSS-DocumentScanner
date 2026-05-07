---
id: faq
title: FAQ
sidebar_position: 7
slug: /faq
---

# Foire Aux Questions

Trouvez des réponses aux questions courantes sur OSS Document Scanner et OSS CardWallet.

## Général

### Ces applications sont-elles vraiment gratuites ?

Oui ! OSS Document Scanner et OSS CardWallet sont entièrement gratuits et open source. Il n'y a pas de publicités, pas d'achats intégrés, et pas de fonctionnalités premium verrouillées derrière un paywall.

### Mes données sont-elles privées ?

Absolument. Les deux applications :
- Traitent toutes les données sur votre appareil
- Ne téléchargent jamais de données vers aucun serveur (sauf si vous configurez la synchronisation)
- N'incluent pas d'analytics ou de suivi
- Sont open source pour que vous puissiez vérifier vous-même

### Quelle est la différence entre les deux applications ?

| OSS Document Scanner | OSS CardWallet |
|---------------------|----------------|
| Numériser des documents papier | Stocker des cartes de fidélité/membres |
| Détection automatique des bords | Scan de codes-barres |
| Export PDF avec OCR | Plusieurs formats de codes-barres |
| Organisation des documents | Organisation des cartes |

Les deux applications partagent des fonctionnalités communes comme la synchronisation cloud, la sauvegarde et un design respectueux de la vie privée.

### Où puis-je obtenir de l'aide ?

- **GitHub Issues** : [Signaler des bugs ou demander des fonctionnalités](https://github.com/ossappscollective/OSS-DocumentScanner/issues)
- **Cette Documentation** : Parcourez les guides et tutoriels
- **Code Source** : Consultez le [code open source](https://github.com/ossappscollective/OSS-DocumentScanner)

### Comment puis-je contribuer ?

- **Traductions** : [Aidez à traduire sur Weblate](https://hosted.weblate.org/engage/oss-document-scanner/)
- **Code** : Soumettez des pull requests sur GitHub
- **Rapports de Bugs** : Signalez les problèmes que vous trouvez
- **Documentation** : Améliorez cette documentation
- **Sponsoring** : [Soutenez le développeur](https://github.com/sponsors/farfromrefug)

## Numérisation

### Pourquoi la détection des bords ne fonctionne-t-elle pas ?

Causes courantes et solutions :

1. **Mauvais éclairage** : Assurez un éclairage uniforme et adéquat
2. **Faible contraste** : Utilisez un fond contrastant (document sombre sur surface claire ou inversement)
3. **Reflets** : Évitez les surfaces brillantes qui créent des reflets
4. **Objectif sale** : Nettoyez l'objectif de votre appareil photo
5. **Fond complexe** : Utilisez un fond uni de couleur unie

### Puis-je numériser des documents multi-pages ?

Oui ! Utilisez le mode de numérisation par lots :

1. Appuyez sur le bouton appareil photo
2. Numérisez la première page
3. Continuez à numériser sans enregistrer
4. Toutes les pages sont combinées en un seul document
5. Appuyez sur Terminé quand vous avez fini

### Quelle résolution dois-je utiliser ?

| Cas d'Utilisation | Résolution Recommandée |
|-------------------|------------------------|
| Référence rapide | Standard |
| Documents généraux | Élevée |
| Documents importants | Maximum |
| Traitement OCR | Élevée ou Maximum |

### Puis-je numériser en faible luminosité ?

Oui, mais la qualité peut en souffrir. Conseils pour la faible luminosité :

- Activez le flash
- Utilisez le mode capture manuel
- Maintenez l'appareil très stable
- Envisagez d'utiliser un éclairage supplémentaire
- Activez l'amélioration automatique après la capture

## Édition

### Comment faire pivoter une page ?

1. Ouvrez le document
2. Sélectionnez la page
3. Appuyez sur le bouton de rotation
4. Choisissez la direction de rotation

### Puis-je recadrer après la numérisation ?

Oui :

1. Ouvrez le document
2. Sélectionnez la page
3. Appuyez sur le bouton recadrer/éditer
4. Ajustez la zone de recadrage
5. Appuyez sur Appliquer

### Quels filtres sont disponibles ?

- **Original** : Aucun traitement
- **Noir et Blanc** : Binaire pur, idéal pour le texte
- **Niveaux de Gris** : Supprime la couleur, garde les tons
- **Amélioré** : Amélioration auto couleur/contraste
- **Document** : Optimisé pour la lisibilité des documents

### Comment fonctionne l'OCR ?

L'OCR (Reconnaissance Optique de Caractères) extrait le texte des images :

1. Ouvrez une page numérisée
2. Appuyez sur le bouton OCR
3. Sélectionnez la ou les langues
4. Le traitement se fait sur l'appareil
5. Le texte peut être copié ou recherché

## Export

### Quels formats d'export sont supportés ?

- **PDF** : Standard et avec couche de texte OCR
- **JPEG** : Images compressées
- **PNG** : Images sans perte
- **TIFF** : Format d'archivage professionnel

### Comment réduire la taille des fichiers PDF ?

1. Utilisez un paramètre de qualité inférieur
2. Appliquez le filtre Noir et Blanc
3. Utilisez la compression JPEG
4. Réduisez la résolution (150 DPI pour l'email)

### Puis-je combiner plusieurs documents en un seul PDF ?

Oui :

1. Sélectionnez plusieurs documents dans la vue liste
2. Appuyez sur Exporter
3. Choisissez "PDF Unique"
4. Tous les documents sont combinés

### Comment envoyer une numérisation par email ?

1. Ouvrez le document
2. Appuyez sur Partager
3. Choisissez le format (PDF recommandé)
4. Sélectionnez votre application email
5. Le document est joint à un nouvel email

## Synchronisation & Sauvegarde

### Quels services cloud sont supportés ?

Synchronisation directe via WebDAV :
- Nextcloud
- ownCloud
- NAS Synology
- Tout serveur WebDAV

Pour d'autres services, exportez et téléchargez manuellement.

### Comment configurer la synchronisation ?

Voir notre guide détaillé [Synchronisation et Sauvegarde](/sync-and-backup).

### Que se passe-t-il si je modifie sur plusieurs appareils ?

La modification la plus récente l'emporte généralement. L'application va :
1. Détecter le conflit
2. Comparer les horodatages
3. Garder la version la plus récente
4. Optionnellement archiver l'ancienne version

### Les sauvegardes sont-elles chiffrées ?

Les sauvegardes sont des fichiers ZIP qui ne sont pas chiffrés par défaut. Pour les documents sensibles :
- Activez le chiffrement des documents dans les paramètres de l'app
- Stockez les sauvegardes dans un stockage chiffré
- Utilisez un stockage cloud chiffré

## Dépannage

### L'application plante au démarrage

Essayez ces étapes :

1. Redémarrez votre appareil
2. Videz le cache de l'app (Paramètres > Apps > OSS Document Scanner > Vider le cache)
3. Mettez à jour vers la dernière version
4. Réinstallez l'application (faites une sauvegarde d'abord !)

### Les numérisations sont floues

- Maintenez l'appareil stable pendant la capture
- Assurez un bon éclairage
- Attendez la mise au point automatique
- Nettoyez l'objectif de l'appareil photo
- Essayez la capture manuelle avec appui pour la mise au point

### L'export PDF échoue

- Vérifiez l'espace de stockage disponible
- Essayez un paramètre de qualité inférieur
- Réduisez le nombre de pages
- Redémarrez l'app et réessayez

### La synchronisation ne fonctionne pas

1. Vérifiez la connexion Internet
2. Vérifiez les identifiants du serveur
3. Testez la connexion dans les paramètres
4. Vérifiez que le serveur est accessible
5. Consultez les logs du serveur pour les erreurs

### L'OCR donne un mauvais texte

- Utilisez une qualité de capture plus élevée
- Appliquez le filtre Noir et Blanc
- Sélectionnez la bonne langue
- Assurez-vous que le texte est clairement visible
- Essayez différents paramètres de seuil

## Technique

### Quelles plateformes sont supportées ?

- **Android** : 7.0 (API 24) et supérieur
- **iOS** : 13.0 et supérieur

### Comment l'application est-elle construite ?

OSS Document Scanner est construit avec :
- **NativeScript** : Framework cross-platform
- **Svelte** : Composants UI
- **OpenCV** : Traitement d'images
- **Tesseract** : Moteur OCR

### Où se trouve le code source ?

Répertoires clés dans le [dépôt GitHub](https://github.com/ossappscollective/OSS-DocumentScanner) :

| Répertoire | Contenu |
|------------|---------|
| `app/` | Code principal de l'application |
| `app/components/` | Composants UI |
| `app/services/` | Logique métier |
| `opencv/` | Intégration OpenCV |
| `plugin-nativeprocessor/` | Plugin de traitement natif |
| `cpp/` | Code natif C++ |

### Comment puis-je compiler depuis les sources ?

Voir la [section Configuration de Build](https://github.com/ossappscollective/OSS-DocumentScanner#building-setup) dans le README principal.

## Intégration de Captures d'Écran Maestro

Pour les tests automatisés et la documentation, OSS Document Scanner supporte Maestro pour la génération de captures d'écran.

### Comment Générer des Captures d'Écran avec Maestro

1. Installez Maestro : https://maestro.mobile.dev/
2. Écrivez des flows Maestro qui naviguent dans les fonctionnalités de l'app
3. Utilisez la commande `- takeScreenshot` pour capturer les écrans
4. Placez les captures générées dans `docs-site/static/img/`

### Script de Placement des Captures

```bash
#!/bin/bash
# copy-screenshots.sh
# Exécuter après les tests Maestro pour copier les captures vers le site de docs

MAESTRO_OUTPUT="./maestro-screenshots"
DOCS_IMG="./docs-site/static/img"

# Copier et renommer les captures
cp "$MAESTRO_OUTPUT/capture.png" "$DOCS_IMG/capture-1.png"
cp "$MAESTRO_OUTPUT/edit.png" "$DOCS_IMG/edit-1.png"
cp "$MAESTRO_OUTPUT/export.png" "$DOCS_IMG/export-1.png"
cp "$MAESTRO_OUTPUT/sync.png" "$DOCS_IMG/sync-1.png"
cp "$MAESTRO_OUTPUT/settings.png" "$DOCS_IMG/settings-1.png"

echo "Captures copiées vers le site de docs"
```

### Convention de Nommage des Captures

Gardez ces noms de fichiers exacts pour que les captures générées par Maestro puissent remplacer les placeholders :

- `capture-1.png` - Écran de capture principal
- `edit-1.png` - Écran d'édition/recadrage
- `export-1.png` - Options d'export
- `sync-1.png` - Paramètres de synchronisation
- `settings-1.png` - Écran des paramètres

## Vous Avez Encore des Questions ?

Si votre question n'est pas répondue ici :

1. Cherchez dans les [issues GitHub existantes](https://github.com/ossappscollective/OSS-DocumentScanner/issues)
2. [Créez une nouvelle issue](https://github.com/ossappscollective/OSS-DocumentScanner/issues/new) avec votre question
3. Consultez le [code source](https://github.com/ossappscollective/OSS-DocumentScanner) pour les détails d'implémentation
