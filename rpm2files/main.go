package main

import (
	"archive/tar"
	"compress/gzip"
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"strings"

	"github.com/bazelbuild/rules_go/go/runfiles"
)

type compositeCloser struct {
	io.Reader
	closer func() error
}

func (c *compositeCloser) Close() error {
	if c.closer != nil {
		return c.closer()
	}
	return nil
}

type extractSpec struct {
	reqPath string
	outPath string
	isExec  bool
}

type tarEntry struct {
	header  *tar.Header
	content []byte
}

func cleanTarPath(p string) string {
	p = path.Clean(p)
	p = strings.TrimPrefix(p, "/")
	p = strings.TrimPrefix(p, "./")
	return p
}

func findZstd() (string, error) {
	if r, err := runfiles.New(); err == nil {
		for _, target := range []string{"zstd/zstd_cli", "zstd/zstd_cli.exe", "zstd/zstd"} {
			if loc, err := r.Rlocation(target); err == nil && loc != "" {
				if _, statErr := os.Stat(loc); statErr == nil {
					return loc, nil
				}
			}
		}
	}
	// Fallback: check adjacent directory of the executable (e.g. In prebuilt bundle)
	if execPath, err := os.Executable(); err == nil {
		dir := filepath.Dir(execPath)
		for _, name := range []string{"zstd", "zstd_cli", "zstd.exe"} {
			p := filepath.Join(dir, name)
			if _, statErr := os.Stat(p); statErr == nil {
				return p, nil
			}
		}
	}
	return "", fmt.Errorf("zstd binary not found in runfiles or executable directory; host PATH is not used")
}

func openArchive(archivePath string) (io.ReadCloser, error) {
	f, err := os.Open(archivePath)
	if err != nil {
		return nil, err
	}

	magic := make([]byte, 4)
	n, err := f.Read(magic)
	if err != nil && err != io.EOF {
		f.Close()
		return nil, err
	}
	if _, err := f.Seek(0, io.SeekStart); err != nil {
		f.Close()
		return nil, err
	}

	// Gzip magic: 0x1f, 0x8b
	if n >= 2 && magic[0] == 0x1f && magic[1] == 0x8b {
		gzr, err := gzip.NewReader(f)
		if err != nil {
			f.Close()
			return nil, fmt.Errorf("failed to create gzip reader: %w", err)
		}
		return &compositeCloser{
			Reader: gzr,
			closer: func() error {
				gzr.Close()
				return f.Close()
			},
		}, nil
	}

	// Zstd magic: 0x28, 0xb5, 0x2f, 0xfd
	if (n >= 4 && magic[0] == 0x28 && magic[1] == 0xb5 && magic[2] == 0x2f && magic[3] == 0xfd) ||
		strings.HasSuffix(archivePath, ".zst") || strings.HasSuffix(archivePath, ".tzst") {
		f.Close()
		zstdBin, err := findZstd()
		if err != nil {
			return nil, fmt.Errorf("failed to decompress zstd archive %s: %w", archivePath, err)
		}
		cmd := exec.Command(zstdBin, "-d", "-c", archivePath)
		stdout, err := cmd.StdoutPipe()
		if err != nil {
			return nil, fmt.Errorf("failed to open zstd pipe: %w", err)
		}
		if err := cmd.Start(); err != nil {
			return nil, fmt.Errorf("failed to start zstd (%s): %w", zstdBin, err)
		}
		return &compositeCloser{
			Reader: stdout,
			closer: func() error {
				stdout.Close()
				return cmd.Wait()
			},
		}, nil
	}

	// Plain tar
	return f, nil
}

func resolveMember(members map[string]*tarEntry, name string, visited map[string]bool, depth int) (*tarEntry, error) {
	if depth > 64 {
		return nil, fmt.Errorf("exceeded maximum symlink depth (64) while resolving %s", name)
	}
	cleanName := cleanTarPath(name)
	if visited[cleanName] {
		return nil, fmt.Errorf("cyclic symlink detected at %s", name)
	}
	visited[cleanName] = true

	entry, ok := members[cleanName]
	if !ok {
		return nil, fmt.Errorf("path %s not found in archive", name)
	}

	if entry.header.Typeflag == tar.TypeSymlink || entry.header.Typeflag == tar.TypeLink {
		target := entry.header.Linkname
		if !strings.HasPrefix(target, "/") {
			target = path.Clean(path.Join(path.Dir(cleanName), target))
		}
		target = cleanTarPath(target)
		return resolveMember(members, target, visited, depth+1)
	}

	return entry, nil
}

func parseBool(s string) bool {
	s = strings.ToLower(strings.TrimSpace(s))
	return s == "1" || s == "true" || s == "yes" || s == "y"
}

func parseArgsFile(argsFilePath string) ([]extractSpec, error) {
	data, err := os.ReadFile(argsFilePath)
	if err != nil {
		return nil, fmt.Errorf("failed to read args file %s: %w", argsFilePath, err)
	}

	var specs []extractSpec
	lines := strings.Split(string(data), "\n")
	for lineNum, line := range lines {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		parts := strings.Fields(line)
		if len(parts) != 3 {
			return nil, fmt.Errorf("invalid format in args file %s line %d: expected 3 fields (<req_path> <out_path> <is_exec>), got %d (%q)", argsFilePath, lineNum+1, len(parts), line)
		}
		specs = append(specs, extractSpec{
			reqPath: parts[0],
			outPath: parts[1],
			isExec:  parseBool(parts[2]),
		})
	}
	return specs, nil
}

func main() {
	var archiveFlag string
	var argsFileFlag string

	flag.StringVar(&archiveFlag, "archive", "", "Path to the input archive file (.tar, .tar.gz)")
	flag.StringVar(&argsFileFlag, "args-file", "", "Path to the args file containing extraction specs (<req_path> <out_path> <is_exec>)")

	flag.Usage = func() {
		fmt.Fprintf(flag.CommandLine.Output(), "Usage: %s --archive <archive> --args-file <args_file>\n", os.Args[0])
		flag.PrintDefaults()
	}
	flag.Parse()

	archivePath := archiveFlag
	argsFile := argsFileFlag

	// Support positional parameters or @args_file
	posArgs := flag.Args()
	if archivePath == "" && len(posArgs) > 0 {
		archivePath = posArgs[0]
		posArgs = posArgs[1:]
	}
	if argsFile == "" && len(posArgs) > 0 {
		argsFile = strings.TrimPrefix(posArgs[0], "@")
		posArgs = posArgs[1:]
	}

	if archivePath == "" {
		flag.Usage()
		os.Exit(0)
	}

	if argsFile == "" {
		fmt.Fprintf(os.Stderr, "Error: missing required args file (--args-file <file>)\n")
		flag.Usage()
		os.Exit(1)
	}

	specs, err := parseArgsFile(argsFile)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}

	if len(specs) == 0 {
		// Nothing to extract
		os.Exit(0)
	}

	rc, err := openArchive(archivePath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error opening archive %s: %v\n", archivePath, err)
		os.Exit(1)
	}
	defer rc.Close()

	tr := tar.NewReader(rc)
	members := make(map[string]*tarEntry)

	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error reading tar archive %s: %v\n", archivePath, err)
			os.Exit(1)
		}

		var content []byte
		if hdr.Typeflag == tar.TypeReg || hdr.Typeflag == tar.TypeRegA {
			content, err = io.ReadAll(tr)
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error reading tar member %s: %v\n", hdr.Name, err)
				os.Exit(1)
			}
		}

		members[cleanTarPath(hdr.Name)] = &tarEntry{
			header:  hdr,
			content: content,
		}
	}

	for _, spec := range specs {
		visited := make(map[string]bool)
		entry, err := resolveMember(members, spec.reqPath, visited, 0)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error resolving member %s in %s: %v\n", spec.reqPath, archivePath, err)
			os.Exit(1)
		}

		outDir := filepath.Dir(spec.outPath)
		if err := os.MkdirAll(outDir, 0755); err != nil {
			fmt.Fprintf(os.Stderr, "Error creating directory %s: %v\n", outDir, err)
			os.Exit(1)
		}

		var perm os.FileMode = 0644
		if spec.isExec {
			perm = 0755
		}

		if err := os.WriteFile(spec.outPath, entry.content, perm); err != nil {
			fmt.Fprintf(os.Stderr, "Error writing file %s: %v\n", spec.outPath, err)
			os.Exit(1)
		}
		if err := os.Chmod(spec.outPath, perm); err != nil {
			fmt.Fprintf(os.Stderr, "Error setting permissions on %s: %v\n", spec.outPath, err)
			os.Exit(1)
		}
	}
}
