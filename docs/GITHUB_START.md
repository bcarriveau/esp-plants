# Starting the GitHub repository

The ZIP package intentionally does not contain a `.git` directory.

After extracting it:

```bash
cd lillygo_t5_plant_monitor_github_baseline
git init
git add .
git commit -m "Initial plant monitor GitHub baseline"
git branch -M main
```

Create an empty GitHub repository, then:

```bash
git remote add origin <your-repository-url>
git push -u origin main
```

## Recommended first tag

Do **not** tag the alpha baseline as a stable release until the release checklist
has passed.

After physical verification, choose the first stable version deliberately.

## Branching

For normal work, keep `main` as the known-good branch and use short-lived
feature/fix branches for risky changes.

Examples:

```text
feature/t5-dashboard
feature/sensor-config
fix/watering-watch
fix/provisioning
```

Merge only after the affected firmware is built and the relevant hardware test
passes.
