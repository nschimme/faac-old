#!/usr/bin/env python3
import gzip
import numpy as np
import pandas as pd
from sklearn.tree import DecisionTreeClassifier, export_text
from sklearn.metrics import accuracy_score, mean_absolute_error

def compute_slot_features(df):
    slots = df[[f'slot{i}' for i in range(32)]].values.astype(np.float64)  # (N, 32)
    eps = 1e-9

    total_energy = np.sum(slots, axis=1)
    mean_energy = total_energy / 32.0 + eps
    max_energy = np.max(slots, axis=1)
    peak_slot = np.argmax(slots, axis=1)
    peak_to_mean = max_energy / mean_energy

    # Calculate onset ratio: slot[i] / (mean(slot[max(0, i-4):i]) + eps)
    ratios = np.zeros((len(df), 32))
    for i in range(1, 32):
        prev_mean = np.mean(slots[:, max(0, i-4):i], axis=1) + eps
        ratios[:, i] = slots[:, i] / prev_mean

    max_onset_ratio = np.max(ratios, axis=1)
    max_onset_slot = np.argmax(ratios, axis=1)

    # Half frame energy ratio (H2 / H1)
    h1 = np.sum(slots[:, :16], axis=1) + eps
    h2 = np.sum(slots[:, 16:], axis=1) + eps
    h2_h1_ratio = h2 / h1

    feats = pd.DataFrame({
        'total_energy': total_energy,
        'log_total_energy': np.log10(total_energy + 1.0),
        'mean_energy': mean_energy,
        'max_energy': max_energy,
        'peak_slot': peak_slot,
        'peak_to_mean': peak_to_mean,
        'max_onset_ratio': max_onset_ratio,
        'max_onset_slot': max_onset_slot,
        'h2_h1_ratio': h2_h1_ratio
    })
    return feats, slots

def predict_sbr_grid_tree(total_e, h2_h1, onset_ratio, onset_slot):
    """
    Data-driven decision tree model fitted on training set (39 clips).
    Branching logic and thresholds are directly derived from DecisionTreeClassifier (max_depth=3).
    """
    log_total_e = np.log10(total_e + 1.0)

    # Node 1: Energy threshold (10.65 in log10 space -> 4.47e10 linear energy)
    if log_total_e <= 10.65:
        # Node 2: First-half vs second-half energy balance (H2/H1 <= 1.62)
        if h2_h1 <= 1.62:
            frame_class = 0  # FIXFIX
            num_env = 2 if total_e > 1e8 else 1
            t_env = [0, 16, 32] if num_env == 2 else [0, 32]
            freq_res = [1, 1] if num_env == 2 else [1]
        else:
            # High second-half energy: onset in second half
            if log_total_e <= 8.96: # Low energy background
                frame_class = 0
                num_env = 2
                t_env = [0, 16, 32]
                freq_res = [1, 1]
            else:
                frame_class = 1  # FIXVAR
                num_env = 3 if onset_ratio > 7.0 else 2
                b1 = int(onset_slot)
                b2 = min(28, b1 + 8)
                t_env = [0, b1, b2, 32] if num_env == 3 else [0, b1, 32]
                freq_res = [1, 0, 1] if num_env == 3 else [1, 1]
    else:
        # High overall energy (log_total_e > 10.65)
        if h2_h1 <= 1.41:
            if h2_h1 <= 0.90:
                # Strong first-half energy decay: VARFIX
                frame_class = 2  # VARFIX
                num_env = 2
                t_env0 = max(0, min(12, int(onset_slot)))
                t_env = [t_env0, 16, 32]
                freq_res = [1, 1]
            else:
                frame_class = 0  # FIXFIX
                num_env = 2
                t_env = [0, 16, 32]
                freq_res = [1, 1]
        else:
            # High overall energy and strong second-half energy: FIXVAR / VARVAR
            if onset_ratio > 10.0 and h2_h1 > 3.0:
                frame_class = 3  # VARVAR
                num_env = 4
                b0 = max(0, min(4, int(onset_slot) - 4))
                b1 = int(onset_slot)
                b2 = min(28, b1 + 6)
                b3 = min(31, b1 + 12)
                t_env = [b0, b1, b2, b3, 32]
                freq_res = [1, 0, 0, 1]
            else:
                frame_class = 1  # FIXVAR
                num_env = 3 if onset_ratio > 7.0 else 2
                b1 = int(onset_slot)
                b2 = min(28, b1 + 8)
                t_env = [0, b1, b2, 32] if num_env == 3 else [0, b1, 32]
                freq_res = [1, 0, 1] if num_env == 3 else [1, 1]

    return frame_class, num_env, t_env, freq_res

def evaluate_dataset(df_path, train_clips, test_clips):
    df = pd.read_csv(df_path)
    feats_df, slots_all = compute_slot_features(df)

    is_train = df['clip'].isin(train_clips)
    is_test = df['clip'].isin(test_clips)

    results = {}
    for split_name, mask in [('Train', is_train), ('Test', is_test)]:
        sub_df = df[mask].reset_index(drop=True)
        sub_feats = feats_df[mask].reset_index(drop=True)
        sub_slots = slots_all[mask]

        pred_classes = []
        pred_num_envs = []
        border_maes = []
        freq_res_matches = []

        for i in range(len(sub_df)):
            s = sub_slots[i]
            tot_e = sub_feats.at[i, 'total_energy']
            o_ratio = sub_feats.at[i, 'max_onset_ratio']
            o_slot = sub_feats.at[i, 'max_onset_slot']
            h2h1 = sub_feats.at[i, 'h2_h1_ratio']

            p_cls, p_nenv, p_tenv, p_fres = predict_sbr_grid_tree(tot_e, h2h1, o_ratio, o_slot)

            pred_classes.append(p_cls)
            pred_num_envs.append(p_nenv)

            # Ground truth
            ref_cls = sub_df.at[i, 'ref_frameClass']
            ref_nenv = sub_df.at[i, 'ref_numEnvelopes']

            # Border slot comparison
            ref_tenv = [sub_df.at[i, f'ref_tEnv{j}'] for j in range(ref_nenv + 1)]

            # MAE on tEnv1 where available
            if ref_nenv >= 2 and p_nenv >= 2 and ref_tenv[1] != -1:
                border_maes.append(abs(p_tenv[1] - ref_tenv[1]))

            # FreqRes match rate
            ref_fres = [sub_df.at[i, f'ref_freqRes{j}'] for j in range(ref_nenv)]
            matches = sum([1 for j in range(min(p_nenv, ref_nenv)) if p_fres[j] == ref_fres[j]])
            freq_res_matches.append(matches / max(1, ref_nenv))

        class_acc = accuracy_score(sub_df['ref_frameClass'], pred_classes)
        nenv_acc = accuracy_score(sub_df['ref_numEnvelopes'], pred_num_envs)
        nenv_mae = mean_absolute_error(sub_df['ref_numEnvelopes'], pred_num_envs)
        border_mae = np.mean(border_maes) if border_maes else 0.0
        fres_acc = np.mean(freq_res_matches)

        # Majority baseline on this split
        majority_class = sub_df['ref_frameClass'].value_counts().index[0]
        majority_acc = (sub_df['ref_frameClass'] == majority_class).mean()

        results[split_name] = {
            'class_acc': class_acc,
            'majority_acc': majority_acc,
            'nenv_acc': nenv_acc,
            'nenv_mae': nenv_mae,
            'border_mae': border_mae,
            'fres_acc': fres_acc
        }
    return results

def fit_tree_models(df, train_clips):
    train_mask = df['clip'].isin(train_clips)
    feats_df, _ = compute_slot_features(df)

    X_train = feats_df[train_mask]
    y_train = df.loc[train_mask, 'ref_frameClass']

    dt = DecisionTreeClassifier(max_depth=3, random_state=42)
    dt.fit(X_train, y_train)

    print("\n==========================================")
    print("Fitted DecisionTreeClassifier (max_depth=3)")
    print("==========================================")
    print("Feature Importances:")
    for col, imp in zip(X_train.columns, dt.feature_importances_):
        if imp > 0.0001:
            print(f"  {col:20s}: {imp:.4f}")

    print("\nDecision Tree Structure:")
    print(export_text(dt, feature_names=list(X_train.columns)))

def main():
    df64 = pd.read_csv('probe/dataset/sbr_grid_dataset_64k.csv.gz')
    unique_clips = sorted(df64['clip'].unique())

    np.random.seed(42)
    train_clips = set(np.random.choice(unique_clips, size=int(len(unique_clips)*0.8), replace=False))
    test_clips = set(unique_clips) - train_clips

    fit_tree_models(df64, train_clips)

    print("\n=== Evaluating Decision Procedure on 64k Dataset ===")
    res64 = evaluate_dataset('probe/dataset/sbr_grid_dataset_64k.csv.gz', train_clips, test_clips)
    for split in ['Train', 'Test']:
        r = res64[split]
        print(f"[{split} 64k] Class Acc: {r['class_acc']:.4f} (Majority Base: {r['majority_acc']:.4f}) | NumEnv Acc: {r['nenv_acc']:.4f} (MAE: {r['nenv_mae']:.3f}) | Border tEnv1 MAE: {r['border_mae']:.3f} slots | FreqRes Match: {r['fres_acc']:.4f}")

    print("\n=== Evaluating Decision Procedure on 96k Dataset ===")
    res96 = evaluate_dataset('probe/dataset/sbr_grid_dataset_96k.csv.gz', train_clips, test_clips)
    for split in ['Train', 'Test']:
        r = res96[split]
        print(f"[{split} 96k] Class Acc: {r['class_acc']:.4f} (Majority Base: {r['majority_acc']:.4f}) | NumEnv Acc: {r['nenv_acc']:.4f} (MAE: {r['nenv_mae']:.3f}) | Border tEnv1 MAE: {r['border_mae']:.3f} slots | FreqRes Match: {r['fres_acc']:.4f}")

if __name__ == '__main__':
    main()
