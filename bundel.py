import os

def generate_code_summary(root_dir, output_file):
    # Extensies en bestanden die we willen inlezen
    valid_extensions = {'.cpp', '.hpp', '.h', '.proto', '.md', '.yml'}
    target_files = {'CMakeLists.txt', 'Dockerfile'}
    
    # Mappen die we absoluut willen negeren om vervuiling te voorkomen
    ignore_dirs = {'.git', 'build', 'liboqs', 'external', '__pycache__'}

    with open(output_file, 'w', encoding='utf-8') as outfile:
        # 1. Eerst de mappenstructuur uitschrijven voor de architectuur-context
        outfile.write("### DIRECTORY STRUCTURE ###\n")
        for root, dirs, files in os.walk(root_dir):
            # Verwijder genegeerde mappen uit de loop
            dirs[:] = [d for d in dirs if d not in ignore_dirs]
            
            level = root.replace(root_dir, '').count(os.sep)
            indent = ' ' * 4 * level
            outfile.write(f"{indent}{os.path.basename(root)}/\n")
            subindent = ' ' * 4 * (level + 1)
            for f in files:
                ext = os.path.splitext(f)[1]
                if ext in valid_extensions or f in target_files:
                    outfile.write(f"{subindent}{f}\n")
        
        outfile.write("\n\n### FILE CONTENTS ###\n")
        
        # 2. Inhoud van de bestanden uitschrijven
        for root, dirs, files in os.walk(root_dir):
            dirs[:] = [d for d in dirs if d not in ignore_dirs]
            for file in files:
                ext = os.path.splitext(file)[1]
                if ext in valid_extensions or file in target_files:
                    filepath = os.path.join(root, file)
                    outfile.write(f"\n{'='*60}\n")
                    outfile.write(f"FILE: {os.path.relpath(filepath, root_dir)}\n")
                    outfile.write(f"{'='*60}\n\n")
                    
                    try:
                        with open(filepath, 'r', encoding='utf-8') as infile:
                            outfile.write(infile.read())
                    except Exception as e:
                        outfile.write(f"[Fout bij inlezen van dit bestand: {e}]\n")

if __name__ == "__main__":
    # Configureer hier de root-map (huidige map) en de output naam
    root_directory = '.'
    output_filename = 'chronos_full_context.txt'
    
    print(f"Start met bundelen van C++ bestanden in {output_filename}...")
    generate_code_summary(root_directory, output_filename)
    print("Klaar! Je kunt het tekstbestand nu uploaden.")