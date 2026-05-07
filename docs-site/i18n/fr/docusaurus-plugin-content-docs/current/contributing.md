---
id: contributing
title: Contribuer à la Documentation
sidebar_position: 8
slug: /contributing
---

# Contribuer à la Documentation

Aidez à améliorer cette documentation ! Nous accueillons les contributions de la communauté.

## Démarrage Rapide

1. **Forkez le dépôt** : [github.com/ossappscollective/OSS-DocumentScanner](https://github.com/ossappscollective/OSS-DocumentScanner)

2. **Clonez votre fork** :
   ```bash
   git clone https://github.com/VOTRE_UTILISATEUR/OSS-DocumentScanner.git
   cd OSS-DocumentScanner/docs-site
   ```

3. **Installez les dépendances** :
   ```bash
   npm install
   ```

4. **Démarrez le serveur de développement** :
   ```bash
   npm start
   ```

5. **Faites vos modifications** aux fichiers dans `docs-site/docs/`

6. **Soumettez une pull request**

## Structure de la Documentation

```
docs-site/
├── docs/                    # Fichiers markdown de documentation
│   ├── getting-started.md   # Introduction Document Scanner
│   ├── capture.md           # Fonctionnalités de capture
│   ├── edit-and-enhance.md  # Fonctionnalités d'édition
│   ├── export.md            # Options d'export
│   ├── sync-and-backup.md   # Configuration de synchronisation
│   ├── settings.md          # Tous les paramètres
│   ├── faq.md               # FAQ
│   ├── contributing.md      # Ce fichier
│   └── cardwallet/          # Docs spécifiques à CardWallet
│       ├── getting-started.md
│       ├── scanning-cards.md
│       └── managing-cards.md
├── src/
│   ├── pages/               # Page d'accueil et pages personnalisées
│   └── css/                 # Styles personnalisés
├── static/img/              # Images et captures d'écran
├── docusaurus.config.js     # Configuration du site
└── sidebars.js              # Structure de la barre latérale
```

## Comment Éditer les Pages

### Éditer des Pages Existantes

1. Naviguez vers `docs-site/docs/`
2. Trouvez le fichier markdown que vous voulez éditer
3. Faites vos modifications
4. Prévisualisez avec `npm start`
5. Soumettez une pull request

### Ajouter de Nouvelles Pages

1. Créez un nouveau fichier `.md` dans `docs-site/docs/`
2. Ajoutez le front matter :
   ```markdown
   ---
   id: ma-page
   title: Titre de Ma Page
   sidebar_position: 5
   slug: /ma-page
   ---

   # Titre de Ma Page
   
   Le contenu va ici...
   ```
3. Ajoutez la page à `sidebars.js`

### Ajouter des Captures d'Écran

1. Prenez ou générez des captures d'écran (voir [section Maestro](#captures-décran-maestro))
2. Enregistrez dans `docs-site/static/img/`
3. Référencez dans le markdown :
   ```markdown
   ![Description](/img/ma-capture.png)
   ```

## Captures d'Écran Maestro

Nous utilisons Maestro pour la génération automatisée de captures d'écran :

```bash
# Générer toutes les captures d'écran
./docs-site/maestro/generate-screenshots.sh android
```

Les captures sont enregistrées directement dans `docs-site/static/img/` avec les noms de fichiers corrects.

### Ajouter de Nouveaux Flows de Capture

1. Créez un nouveau flow dans `docs-site/maestro/screenshots/` :
   ```yaml
   # ma-capture.yaml
   appId: com.akylas.documentscanner
   ---
   - launchApp
   - tapOn: "Un Bouton"
   - takeScreenshot: ../../static/img/ma-capture
   ```

2. Ajoutez à `generate-screenshots.sh`

## Traductions

### Traduire la Documentation

1. Générez les fichiers de traduction :
   ```bash
   npm run write-translations -- --locale fr
   ```

2. Copiez les docs à traduire :
   ```bash
   mkdir -p i18n/fr/docusaurus-plugin-content-docs/current
   cp -r docs/* i18n/fr/docusaurus-plugin-content-docs/current/
   ```

3. Traduisez les fichiers markdown copiés

4. Testez vos traductions :
   ```bash
   npm start -- --locale fr
   ```

### Locales Supportées

| Locale | Langue |
|--------|--------|
| `en`   | Anglais (par défaut) |
| `fr`   | Français |
| `es`   | Espagnol |
| `de`   | Allemand |

### Utiliser les Traductions de l'Application

L'application a des traductions dans les fichiers JSON à `app/i18n/` (ex: `app/i18n/en.json`, `app/i18n/fr.json`). Lors de la documentation des paramètres, vous pouvez référencer les clés de traduction pour assurer la cohérence :

```markdown
<!-- Référencer la clé de traduction de l'app pour la précision -->
**Paramètre** : `auto_scan` - "découvrir et ajouter des documents automatiquement"
```

## Guide de Style

### Style d'Écriture

- Utilisez un langage clair et concis
- Écrivez pour les utilisateurs, pas les développeurs
- Utilisez la voix active
- Incluez des exemples pratiques

### Formatage

- Utilisez les titres de façon hiérarchique (H1 → H2 → H3)
- Utilisez les tableaux pour comparer les options
- Utilisez les blocs de code pour les commandes et chemins
- Utilisez les admonitions pour les astuces et avertissements :

```markdown
:::tip
Astuce utile ici
:::

:::warning
Avertissement important ici
:::

:::info
Note informative ici
:::
```

### Images

- Utilisez du texte alt descriptif
- Optimisez les images pour le web
- Utilisez un nommage cohérent : `fonctionnalite-action.png`
- Placez dans `static/img/`

## Construction et Tests

### Développement Local

```bash
npm start          # Démarrer le serveur de dev
npm run build      # Construire pour la production
npm run serve      # Servir le build de production localement
```

### Vérifier les Liens

```bash
npm run build      # Le build échouera sur les liens cassés
```

### Vérifier les Traductions

```bash
npm run build -- --locale fr    # Construire une locale spécifique
```

## Directives pour les Pull Requests

1. **Un sujet par PR** : Gardez les modifications concentrées
2. **Décrivez les modifications** : Expliquez quoi et pourquoi
3. **Testez localement** : Assurez-vous que le build passe
4. **Captures d'écran** : Incluez avant/après pour les changements UI
5. **Liez les issues** : Référencez les issues liées

### Template de PR

```markdown
## Description
Brève description des modifications

## Type de Modification
- [ ] Correction de bug
- [ ] Nouvelle documentation
- [ ] Mise à jour de documentation
- [ ] Traduction

## Tests
- [ ] Construit localement avec `npm run build`
- [ ] Testé sur viewport mobile
- [ ] Vérifié que les liens fonctionnent

## Captures d'écran (si applicable)
```

## Obtenir de l'Aide

- **Questions** : Ouvrez une [Discussion GitHub](https://github.com/ossappscollective/OSS-DocumentScanner/discussions)
- **Bugs** : Ouvrez une [Issue GitHub](https://github.com/ossappscollective/OSS-DocumentScanner/issues)
- **Chat** : Rejoignez la communauté sur GitHub

## Reconnaissance

Les contributeurs sont reconnus dans :
- L'historique des commits Git
- Les notes de version (pour les contributions significatives)
- Le pied de page de cette documentation

Merci d'aider à améliorer la documentation ! 🙏
