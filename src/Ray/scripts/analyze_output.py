
import json
import re
import sys

def process_file(f, all_tests):
    for line in f.readlines():
        m = re.search(r'^Test\s*(\w+).*100.0%.*PSNR:\s*([.\w]+)\/([.\w]+).*Fireflies:\s*([.\w]+)\/([.\w]+).*', line)
        if m:
            test_name, psnr_tested, psnr_threshold, fireflies_tested, fireflies_threshold = m[1], float(m[2]), float(m[3]), int(m[4]), int(m[5])
            if test_name not in all_tests:
                new_test = {}
                new_test['psnr_tested'] = psnr_tested
                new_test['psnr_threshold'] = psnr_threshold
                new_test['fireflies_tested'] = fireflies_tested
                new_test['fireflies_threshold'] = fireflies_threshold

                all_tests[test_name] = new_test
            else:
                test = all_tests[test_name]
                if 'required_samples' not in test:
                    test['psnr_tested'] = min(test['psnr_tested'], psnr_tested)
                    test['fireflies_tested'] = max(test['fireflies_tested'], fireflies_tested)
        else:
            m = re.search(r'^Required sample count for\s*(\w+):\s*(\w+).*', line)
            if m:
                test_name, required_samples = m[1], int(m[2])
                test = all_tests[test_name]
                if 'required_samples' not in test:
                    test.pop('psnr_tested', None)
                    test.pop('fireflies_tested', None)
                    test['required_samples'] = required_samples
                else:
                    test['required_samples'] = max(test['required_samples'], required_samples)

def main():
    all_tests = {}

    # Loop through all test results and keep minimal 'PSNR' and maximal 'Fireflies' values
    for i in range(1, len(sys.argv)):
        try:
            with open(sys.argv[i], "r", encoding='utf-8') as f:
                process_file(f, all_tests)
        except:
            try:
                with open(sys.argv[i], "r", encoding='utf-16') as f:
                    process_file(f, all_tests)
            except:
                print("Failed to process ", sys.argv[i])

    print(json.dumps(all_tests, indent=4))

if __name__ == "__main__":
    main()
