Traceback (most recent call last):
  File "/home/omerpr/BiHS-Bloom/scripts/merge_split_results.py", line 233, in <module>
    main()
  File "/home/omerpr/BiHS-Bloom/scripts/merge_split_results.py", line 229, in main
    merge_final(args.domain, args.params, args.run_dir, args.convergence_dir, args.output_dir)
  File "/home/omerpr/BiHS-Bloom/scripts/merge_split_results.py", line 137, in merge_final
    params_rows = read_csv_files([params_path])
  File "/home/omerpr/BiHS-Bloom/scripts/merge_split_results.py", line 28, in read_csv_files
    with path.open(newline="", encoding="utf-8") as handle:
  File "/usr/lib64/python3.9/pathlib.py", line 1180, in open
    return io.open(self, mode, buffering, encoding, errors, newline,
  File "/usr/lib64/python3.9/pathlib.py", line 1038, in _opener
    return self._accessor.open(self, flags, mode)
FileNotFoundError: [Errno 2] No such file or directory: 'results/split/params/rubik_params.csv'
