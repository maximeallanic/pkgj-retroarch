# pkgj-retroarch

Fork de [PKGj](https://github.com/blastrock/pkgj) adapté pour télécharger des ROMs depuis Archive.org et les installer dans RetroArch sur PS Vita.

> ⚠️ **En cours de développement** — ce fork est une base de travail. Les modifications source (remplacement TSV → Archive.org API, installation .pkg → copie ux0:roms/) sont planifiées.

---

## Installation sur PS Vita

### Prérequis

- PS Vita avec firmware personnalisé (HENkaku / Ensō)
- [VitaShell](https://github.com/TheOfficialFloW/VitaShell) installé
- Accès FTP ou carte mémoire en USB

### Méthode 1 — Navigateur PS Vita (sans PC)

1. Depuis la PS Vita, ouvrez le navigateur internet
2. Accédez aux [Releases GitHub](../../releases) de ce repo
3. Téléchargez le fichier `pkgj-retroarch-vX.X.vpk`
4. Le fichier s'ouvre automatiquement avec VitaShell
5. Confirmez l'installation → l'application apparaît sur la LiveArea

### Méthode 2 — FTP via VitaShell (recommandée)

1. Téléchargez le `.vpk` depuis la page [Releases](../../releases) sur votre PC
2. Lancez **VitaShell** sur la Vita → appuyez sur **SELECT** pour activer le serveur FTP
3. Depuis votre PC, connectez-vous à `ftp://<IP_VITA>:1337`
4. Copiez le fichier `.vpk` dans `ux0:` (ou `uma0:`)
5. Dans VitaShell, naviguez jusqu'au `.vpk` → appuyez sur **X** pour installer
6. Confirmez l'installation

### Méthode 3 — USB via VitaShell

1. Connectez la Vita au PC en USB
2. Dans VitaShell → **X** sur `ux0:` → **Triangle** → **Open decrypted**
3. Copiez le `.vpk` à la racine de `ux0:` via l'explorateur PC
4. Éjectez proprement, revenez dans VitaShell, naviguez jusqu'au `.vpk` → **X** pour installer

---

## Build depuis les sources

```bash
# Prérequis : Docker + vitasdk/vitasdk image
docker run --rm -v $(pwd):/pkgj -w /pkgj vitasdk/vitasdk bash -c "
  apt-get update -qq && apt-get install -y ninja-build g++ python3-pip cmake git
  pip3 install 'poetry<2.0' --break-system-packages
  cd ci && ./setup_conan.sh && cd ..
  mkdir -p ci/build && cd ci/build
  poetry run conan install ../.. --build missing -s build_type=Release --profile:host vita --output-folder .
  poetry run conan build ../.. -s build_type=Release --profile:host vita --output-folder .
"
# Le .vpk se trouve dans ci/build/pkgj.vpk
```

### CI/CD

Chaque push sur `master` déclenche un build automatique (GitHub Actions, `vitasdk/vitasdk`).  
Chaque tag `vX.Y` ou `vX.Y.Z` crée une release avec le `.vpk` en asset téléchargeable.

---

## Architecture des modifications prévues

| Fichier | Modification |
|---|---|
| `src/db.cpp` + `src/db.hpp` | TSV NPS → JSON Archive.org API |
| `src/config.cpp` | URLs NPS → Archive.org par système (SNES, GBA, NES, PS1…) |
| `src/install.cpp` + `.hpp` | `scePromoterUtil` → `sceIoRename` vers `ux0:roms/<system>/` |
| `src/downloader.cpp` + `.hpp` | Dispatcher install → copie ROM |
| `src/download.cpp` | PKG decrypt/decompress → supprimable (ROMs non chiffrés) |

## Licence

Voir [LICENSE](LICENSE) (original PKGj — MIT-like).
